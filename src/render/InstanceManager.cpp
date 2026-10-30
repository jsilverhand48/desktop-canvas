// Implementation of InstanceManager declared in InstanceManager.h.
#include "render/InstanceManager.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

#include "audio/AudioPolicy.h"
#include "core/EventLoop.h"
#include "core/Log.h"
#include "desktop/FullscreenPolicy.h"
#include "display/OutputGeometry.h"
#include "display/Platform.h"
#include "process/ChildSupervisor.h"
#include "process/ExternalProcessInstance.h"
#include "process/HelperLocator.h"
#include "process/LweCommandBuilder.h"
#include "process/SceneHelperInstance.h"
#include "render/WallpaperInstance.h"
#include "source/SceneOptions.h"
#include "source/WorkshopScanner.h"

namespace canvas {

InstanceManager::InstanceManager(EventLoop& loop, Platform* platform,
                                 Config& config, AudioPolicy& audio,
                                 ChildSupervisor& supervisor)
    : loop_(loop),
      platform_(platform),
      config_(config),
      audio_(audio),
      supervisor_(supervisor),
      sceneHelper_(HelperLocator::find("desktop-canvas-scene")),
      nativeSupport_(std::make_unique<NativeSupportCache>(
          NativeSupportCache::defaultPath())) {
    nativeSupport_->load();
    supervisor_.onBroken([this](const std::string& tag) { handleBroken(tag); });
    if (platform_) {
        platform_->demoteWindowsOf(
            [this](pid_t pid) { return supervisor_.ownsPid(pid); });
        platform_->setOnPointer(
            [this](const std::string& output, double x, double y) {
                auto it = slots_.find(output);
                if (it != slots_.end() && it->second.instance)
                    it->second.instance->setPointer(output, x, y);
            });
    }
    rescan();
}

InstanceManager::~InstanceManager() = default;

void InstanceManager::rescan() {
    auto roots = config_.contentRoots.empty() ? WorkshopScanner::defaultRoots()
                                              : config_.contentRoots;
    roots_ = roots;
    items_ = WorkshopScanner::scan(roots);
    ++libraryRevision_;
    if (!config_.sceneAssetsDir.empty()) {
        assetsDir_ = config_.sceneAssetsDir;
    } else if (auto assets = WorkshopScanner::findAssetsDir(roots)) {
        assetsDir_ = assets->string();
    }
    log::info() << "scanned " << items_.size() << " wallpapers from "
                << roots.size() << " root(s)";
}

const WallpaperInfo* InstanceManager::find(const std::string& id) const {
    for (const auto& item : items_)
        if (item.id == id) return &item;
    return nullptr;
}

bool InstanceManager::usable(const std::string& id) const {
    const WallpaperInfo* info = find(id);
    return info && info->type != WallpaperType::Unsupported && !isBroken(id);
}

bool InstanceManager::isBroken(const std::string& id) const {
    return std::find(config_.broken.begin(), config_.broken.end(), id) !=
           config_.broken.end();
}

void InstanceManager::apply() {
    if (!platform_) return;
    auto outputs = platform_->outputs();

    // Drop slots for outputs that disappeared. A merged group loses all its
    // slots here; the survivors are restarted below without the old output.
    std::vector<std::string> stale;
    for (const auto& [name, slot] : slots_) {
        bool exists = std::any_of(outputs.begin(), outputs.end(),
                                  [&](const auto& o) { return o.name == name; });
        if (!exists) stale.push_back(name);
    }
    for (const auto& name : stale) stopInstance(name);
    // Surfaces of vanished outputs go too, after their instances stopped.
    std::erase_if(surfaces_, [&](const auto& entry) {
        return std::none_of(outputs.begin(), outputs.end(), [&](const auto& o) {
            return o.name == entry.first;
        });
    });

    // What each output should show (null = nothing).
    std::map<std::string, const WallpaperInfo*> desired;
    for (const auto& out : outputs) {
        std::string id = resolve(out.name);
        const WallpaperInfo* info = id.empty() ? nullptr : find(id);
        if (info && (info->type == WallpaperType::Unsupported || isBroken(id))) {
            log::warn() << "wallpaper " << id << " not usable on " << out.name
                        << (isBroken(id) ? " (marked broken)"
                                         : " (" + info->unsupportedReason + ")");
            info = nullptr;
        }
        desired[out.name] = info;
    }

    // Which outputs each output's instance should cover. Configured spans
    // come first: when every output of a span is present and shows the same
    // mergeable scene, they become one span group (config order).
    std::map<std::string, std::vector<std::string>> groups;
    std::set<std::string> spanned;
    for (const auto& span : config_.spans) {
        const WallpaperInfo* first = nullptr;
        bool active = !span.empty();
        for (const auto& name : span) {
            auto d = desired.find(name);
            const WallpaperInfo* info = d == desired.end() ? nullptr : d->second;
            if (!info || info->type != WallpaperType::Scene || !mergeable(*info) ||
                (first && info->id != first->id)) {
                active = false;
                break;
            }
            first = info;
        }
        if (!active) continue;
        for (const auto& name : span) {
            groups[name] = span;
            spanned.insert(name);
        }
    }
    for (const auto& out : outputs) {
        if (spanned.count(out.name)) continue;
        const WallpaperInfo* info = desired[out.name];
        if (!info) continue;
        auto& group = groups[out.name];
        if (!mergeable(*info)) {
            group.push_back(out.name);
            continue;
        }
        for (const auto& [name, other] : desired)  // map order: sorted
            if (other && other->id == info->id && !spanned.count(name))
                group.push_back(name);
    }

    // Stop instances that no longer match; this drops every output sharing
    // them, so the start pass below sees them as empty.
    for (const auto& out : outputs) {
        auto it = slots_.find(out.name);
        if (it == slots_.end()) continue;
        const WallpaperInfo* info = desired[out.name];
        if (!info || it->second.instance->wallpaperId() != info->id ||
            it->second.group != groups[out.name])
            stopInstance(out.name);
    }

    // Start what is missing; a merged group starts once for all its outputs.
    for (const auto& out : outputs) {
        const WallpaperInfo* info = desired[out.name];
        if (!info || slots_.count(out.name)) continue;
        startInstance(groups[out.name], *info, spanned.count(out.name) > 0);
    }
}

bool InstanceManager::mergeable(const WallpaperInfo& info) const {
    if (info.type != WallpaperType::Scene || !platform_) return false;
    if (sceneEngineFor(info) != SceneEngine::Lwe) return true;
    return platform_->isWayland();
}

InstanceManager::SceneEngine InstanceManager::sceneEngineFor(
    const WallpaperInfo& info) const {
    const std::string engine = config_.engineFor(info.id);
    if (engine == "lwe" || sceneHelper_.empty()) return SceneEngine::Lwe;
    if (engine == "native") return SceneEngine::Native;
    // "auto": native unless it already said it cannot show this content.
    if (nativeSupport_->find(info.id, contentStamp(info),
                             NativeSupportCache::stampOf(sceneHelper_)))
        return SceneEngine::Lwe;
    return SceneEngine::NativeStrict;
}

void InstanceManager::useNativeSupportCache(const std::filesystem::path& file) {
    nativeSupport_ = std::make_unique<NativeSupportCache>(file);
    nativeSupport_->load();
}

std::string InstanceManager::contentStamp(const WallpaperInfo& info) {
    auto pkg = info.dir / "scene.pkg";
    std::string stamp = NativeSupportCache::stampOf(pkg);
    return stamp.empty() ? NativeSupportCache::stampOf(info.filePath()) : stamp;
}

bool InstanceManager::lweLayerSupported() {
    if (lweLayer_) return *lweLayer_;
    // Blocking popen, once per restartAll(), and only when an lwe scene is
    // actually about to start.
    std::string help = LweCommandBuilder::probeHelp(config_.sceneEnginePath);
    if (help.empty()) {
        log::warn() << "linux-wallpaperengine not found; lwe scenes will not "
                       "render (set scene.engine_path, install it, or use "
                       "scene.engine native/auto)";
    } else if (help.find("--screen-root") == std::string::npos ||
               help.find("--silent") == std::string::npos) {
        log::warn() << "installed linux-wallpaperengine has unexpected flags; "
                       "scene control may misbehave";
    }
    lweLayer_ = LweCommandBuilder::supportsLayer(help);
    if (!help.empty() && !*lweLayer_ && platform_ && platform_->isWayland())
        log::warn() << "linux-wallpaperengine has no --layer flag; scenes use "
                       "its default layer and may cover desktop icons";
    return *lweLayer_;
}

void InstanceManager::startInstance(const std::vector<std::string>& group,
                                    const WallpaperInfo& info, bool span) {
    // Only merged scenes cover more than one output; everything else
    // (video, web, X11 lwe scenes) targets group.front() alone.
    const std::string& outputName = group.front();
    bool muted = audio_.effectiveMuted(info.id);
    // X11 children run windowed at the output geometry and get demoted to
    // the desktop layer; Wayland children target the output by name.
    std::string geometry;
    if (platform_ && !platform_->isWayland()) {
        for (const auto& o : platform_->outputs()) {
            if (o.name == outputName) {
                std::ostringstream geo;
                geo << o.x << "x" << o.y << "x" << o.width << "x" << o.height;
                geometry = geo.str();
            }
        }
    }
    std::shared_ptr<WallpaperInstance> instance;
    switch (info.type) {
        case WallpaperType::Scene:
            instance = makeSceneInstance(group, info, muted, geometry, span);
            break;
        case WallpaperType::Video: {
            // The factory comes from main.cpp, which owns the libmpv
            // backend; builds without libmpv install none.
            if (!videoFactory_) {
                log::error() << "video backend not built (needs libmpv)";
                return;
            }
            RenderSurface* surface = surfaceFor(outputName);
            if (!surface) return;
            instance = videoFactory_(*surface, info, muted);
            break;
        }
        case WallpaperType::Web: {
            std::string helper = HelperLocator::find("desktop-canvas-web");
            if (helper.empty()) {
                log::warn() << "web wallpaper " << info.id
                            << " needs desktop-canvas-web (build with "
                               "DESKTOP_CANVAS_WEB=ON)";
                return;
            }
            std::string url = "file://" + info.filePath().string();
            std::string project = (info.dir / "project.json").string();
            std::string output = outputName;
            bool wayland = platform_ && platform_->isWayland();
            instance = std::make_shared<ExternalProcessInstance>(
                supervisor_, "web:" + outputName, outputName, info.id, muted,
                [helper, url, project, output, geometry, wayland](bool m) {
                    std::vector<std::string> argv{helper, "--url", url,
                                                  "--project", project};
                    if (wayland) {
                        argv.push_back("--output");
                        argv.push_back(output);
                    } else if (!geometry.empty()) {
                        argv.push_back("--geometry");
                        argv.push_back(geometry);
                    }
                    if (!m) argv.push_back("--unmuted");
                    return argv;
                });
            break;
        }
        case WallpaperType::Unsupported:
            return;
    }
    if (!instance) return;
    std::string where;
    for (const auto& name : group) where += (where.empty() ? "" : ", ") + name;
    log::info() << "starting " << toString(info.type) << " wallpaper "
                << info.id << " (" << info.title << ") on " << where;
    instance->start();
    for (const auto& name : group) {
        slots_[name] = Slot{instance, group};
        if (covered_.count(name)) instance->setCovered(name, true);
    }
    pauseApplied_[instance.get()] = false;
    updatePause();  // starts paused when paused or fully covered
}

std::shared_ptr<WallpaperInstance> InstanceManager::makeSceneInstance(
    const std::vector<std::string>& group, const WallpaperInfo& info,
    bool muted, const std::string& geometry, bool span) {
    const std::string& outputName = group.front();
    // Per wallpaper property values: pinned ones always, plus (with
    // scene.randomize_options) a random pick per dropdown, drawn once per
    // launch (each rotation relaunches) and captured below so a restart of
    // the same instance keeps the variant.
    static const Config::WallpaperSettings kNoSettings;
    auto ws = config_.wallpapers.find(info.id);
    const auto& settings =
        ws != config_.wallpapers.end() ? ws->second : kNoSettings;
    PropertyList properties = resolveSceneProperties(
        readUserProperties(info.dir), settings.properties, settings.allowed,
        config_.sceneRandomizeOptions, config_.sceneIncludeNsfwOptions,
        optionRng_);
    for (const auto& [name, value] : properties)
        log::info() << "scene " << info.id << ": " << name << "=" << value;

    SceneEngine engine = sceneEngineFor(info);
    if (engine != SceneEngine::Lwe) {
        SceneHelperInstance::Launch launch;
        launch.helperPath = sceneHelper_;
        launch.wallpaperDir = info.dir.string();
        launch.outputs = group;
        launch.layer = config_.layer;
        launch.fps = config_.fps;
        launch.scaling = config_.scaling;
        launch.assetsDir = assetsDir_;
        launch.properties = properties;
        launch.strict = engine == SceneEngine::NativeStrict;
        launch.span = span;
        auto instance = std::make_shared<SceneHelperInstance>(
            loop_, supervisor_, "native:" + outputName, info.id, muted,
            audio_.volume(), std::move(launch));
        std::string id = info.id;
        instance->setOnUnsupported(
            [this, id](const std::vector<std::string>& features) {
                handleUnsupported(id, features);
            });
        return instance;
    }

    LweOptions opts;
    opts.enginePath = config_.sceneEnginePath;
    opts.wallpaperDir = info.dir.string();
    opts.assetsDir = assetsDir_;
    opts.fps = config_.fps;
    opts.scaling = config_.scaling;
    opts.volume = audio_.volume();
    opts.disableMouse = config_.sceneDisableMouse;
    opts.properties = properties;
    if (mergeable(info)) {
        opts.screenRoots = group;
        opts.span = span;
        if (lweLayerSupported()) opts.layer = config_.layer;
    } else {
        opts.windowGeometry = geometry;
    }
    // The tag names the first output; handleBroken drops the whole group
    // through it.
    return std::make_shared<ExternalProcessInstance>(
        supervisor_, "scene:" + outputName, outputName, info.id, muted,
        [opts](bool m) {
            LweOptions o = opts;
            o.muted = m;
            return LweCommandBuilder::build(o);
        });
}

void InstanceManager::handleUnsupported(
    const std::string& id, const std::vector<std::string>& features) {
    const WallpaperInfo* info = find(id);
    if (!info) return;
    NativeSupportCache::Verdict verdict;
    verdict.reason = "unsupported";
    verdict.features = features;
    verdict.contentStamp = contentStamp(*info);
    verdict.helperStamp = NativeSupportCache::stampOf(sceneHelper_);
    nativeSupport_->record(id, std::move(verdict));
    log::info() << "wallpaper " << id
                << " falls back to linux-wallpaperengine";
    restartWallpaper(id);
}

RenderSurface* InstanceManager::surfaceFor(const std::string& outputName) {
    // Only called right after stopInstance(outputName), so no instance still
    // borrows a surface this replaces.
    auto it = surfaces_.find(outputName);
    if (it != surfaces_.end() && it->second->closed()) {
        log::info() << "replacing retired surface for " << outputName;
        surfaces_.erase(it);
        it = surfaces_.end();
    }
    if (it == surfaces_.end()) {
        auto created = platform_->createSurface(outputName, config_.layer);
        if (!created) return nullptr;
        it = surfaces_.emplace(outputName, std::move(created)).first;
    }
    return it->second.get();
}

void InstanceManager::stopInstance(const std::string& outputName) {
    auto it = slots_.find(outputName);
    if (it == slots_.end()) return;
    // Hold a reference so the instance outlives the erase of its slots.
    std::shared_ptr<WallpaperInstance> instance = it->second.instance;
    instance->stop();
    std::erase_if(slots_, [&](const auto& entry) {
        return entry.second.instance == instance;
    });
    pauseApplied_.erase(instance.get());
}

std::vector<WallpaperInstance*> InstanceManager::instances() const {
    std::vector<WallpaperInstance*> result;
    for (const auto& [name, slot] : slots_)
        if (std::find(result.begin(), result.end(), slot.instance.get()) ==
            result.end())
            result.push_back(slot.instance.get());
    return result;
}

void InstanceManager::handleBroken(const std::string& tag) {
    // tag format: "<kind>:<output>"; for a merged scene, its first output.
    auto pos = tag.find(':');
    if (pos == std::string::npos) return;
    std::string kind = tag.substr(0, pos);
    std::string output = tag.substr(pos + 1);
    auto it = slots_.find(output);
    if (it == slots_.end() || !it->second.instance) return;
    std::string id = it->second.instance->wallpaperId();
    const WallpaperInfo* info = find(id);
    // A crash looping native helper under "auto" hands the wallpaper to lwe
    // instead of blacklisting it; lwe failing as well blacklists as usual.
    if (kind == "native" && info && config_.engineFor(id) == "auto") {
        NativeSupportCache::Verdict verdict;
        verdict.reason = "crash";
        verdict.contentStamp = contentStamp(*info);
        verdict.helperStamp = NativeSupportCache::stampOf(sceneHelper_);
        nativeSupport_->record(id, std::move(verdict));
        log::error() << "native renderer keeps crashing on " << id
                     << ", falling back to linux-wallpaperengine";
        stopInstance(output);
        apply();
        return;
    }
    log::error() << "marking wallpaper " << id << " broken";
    if (!isBroken(id)) {
        config_.broken.push_back(id);
        config_.save(config_.sourcePath);
    }
    stopInstance(output);  // the whole group if merged
    apply();  // possibly assign nothing / other outputs unaffected
}

std::string InstanceManager::resolve(const std::string& output) const {
    auto it = overrides_.find(output);
    if (it != overrides_.end()) return it->second;
    it = overrides_.find("*");
    if (it != overrides_.end()) return it->second;
    return config_.wallpaperFor(output);
}

void InstanceManager::setOverride(const std::string& output,
                                  const std::string& id) {
    if (id.empty())
        overrides_.erase(output);
    else
        overrides_[output] = id;
}

void InstanceManager::clearOverrides() { overrides_.clear(); }

bool InstanceManager::setAssignment(const std::string& output,
                                    const std::string& id,
                                    std::string& error) {
    if (!id.empty()) {
        const WallpaperInfo* info = find(id);
        if (!info) {
            error = "unknown wallpaper id " + id;
            return false;
        }
        if (info->type == WallpaperType::Unsupported) {
            error = "wallpaper " + id + " unsupported: " +
                    info->unsupportedReason;
            return false;
        }
    }
    if (id.empty())
        config_.assignments.erase(output);
    else
        config_.assignments[output] = id;
    config_.save(config_.sourcePath);
    // A rotation override would otherwise hide the choice that was just
    // made, so mirror it into the override map: the manual pick shows now
    // and the next rotation tick takes the output back.
    setOverride(output, id);
    apply();
    return true;
}

void InstanceManager::restartAll() {
    while (!slots_.empty()) stopInstance(slots_.begin()->first);
    lweLayer_.reset();  // engine_path may have changed: probe again
    sceneHelper_ = HelperLocator::find("desktop-canvas-scene");
    apply();
}

void InstanceManager::restartWallpaper(const std::string& id) {
    std::vector<std::string> outputs;
    for (const auto& [name, slot] : slots_)
        if (slot.instance && slot.instance->wallpaperId() == id)
            outputs.push_back(name);
    for (const auto& name : outputs) stopInstance(name);  // no-op if shared
    apply();
}

void InstanceManager::clearBroken(const std::string& id) {
    if (std::erase(config_.broken, id) == 0) return;
    config_.save(config_.sourcePath);
    apply();
}

void InstanceManager::pauseAll() {
    paused_ = true;
    updatePause();
}

void InstanceManager::updatePause() {
    // An instance is "covered" when every output it shows on is covered.
    std::map<WallpaperInstance*, bool> allCovered;
    for (const auto& [name, slot] : slots_) {
        if (!slot.instance) continue;
        bool here = covered_.count(name) > 0;
        auto [it, inserted] = allCovered.emplace(slot.instance.get(), here);
        if (!inserted) it->second = it->second && here;
    }
    for (const auto& [instance, covered] : allCovered) {
        bool pause = paused_ || covered;
        bool& applied = pauseApplied_[instance];
        if (pause == applied) continue;
        applied = pause;
        if (pause)
            instance->pause();
        else
            instance->resume();
    }
}

void InstanceManager::setToplevels(std::vector<ToplevelInfo> windows) {
    toplevels_ = std::move(windows);
    refreshCoverage();
}

void InstanceManager::refreshCoverage() {
    FullscreenRules rules;
    rules.enabled = config_.fullscreenPauseEnabled;
    rules.onlyActive = config_.fullscreenPauseOnlyActive;
    rules.ignoreAppIds = config_.fullscreenIgnoreAppIds;
    auto covered = canvas::coveredOutputs(toplevels_, rules);
    if (covered == covered_) return;
    std::string list;
    for (const auto& name : covered) list += (list.empty() ? "" : ", ") + name;
    log::info() << "fullscreen windows cover: "
                << (list.empty() ? std::string("nothing") : list);
    covered_ = std::move(covered);
    for (const auto& [name, slot] : slots_)
        if (slot.instance) slot.instance->setCovered(name, covered_.count(name) > 0);
    updatePause();
}

void InstanceManager::pointerAtGlobal(double x, double y) {
    if (!platform_) return;
    auto outputs = platform_->outputs();
    const OutputDesc* output = geometry::outputAt(outputs, x, y);
    if (!output) return;
    auto it = slots_.find(output->name);
    if (it != slots_.end() && it->second.instance)
        it->second.instance->setPointer(output->name, x - output->x,
                                        y - output->y);
}

void InstanceManager::resumeAll() {
    paused_ = false;
    updatePause();  // covered instances stay paused
}

void InstanceManager::setMuted(bool muted) {
    audio_.setDefaultMuted(muted);
    config_.audioMuted = muted;
    config_.save(config_.sourcePath);
    for (auto* instance : instances())
        instance->setMuted(audio_.effectiveMuted(instance->wallpaperId()));
}

void InstanceManager::setVolume(int volume) {
    audio_.setVolume(volume);
    std::vector<std::string> relaunch;
    for (auto* instance : instances()) {
        if (instance->setVolume(volume)) continue;
        // Launch time only (lwe, web): restart if the change is audible.
        if (!audio_.effectiveMuted(instance->wallpaperId()))
            relaunch.push_back(instance->wallpaperId());
    }
    for (const auto& id : relaunch) restartWallpaper(id);
}

std::map<std::string, std::string> InstanceManager::running() const {
    std::map<std::string, std::string> result;
    for (const auto& [name, slot] : slots_)
        if (slot.instance) result[name] = slot.instance->wallpaperId();
    return result;
}

}  // namespace canvas
