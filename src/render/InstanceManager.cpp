// Implementation of InstanceManager declared in InstanceManager.h.
#include "render/InstanceManager.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

#include "audio/AudioPolicy.h"
#include "core/EventLoop.h"
#include "core/Log.h"
#include "display/Platform.h"
#include "process/ChildSupervisor.h"
#include "process/ExternalProcessInstance.h"
#include "process/LweCommandBuilder.h"
#include "render/WallpaperInstance.h"
#include "source/SceneOptions.h"
#include "source/WorkshopScanner.h"

#if defined(CANVAS_HAVE_WAYLAND) || defined(CANVAS_HAVE_X11)
#define CANVAS_HAVE_VIDEO 1
#include "render/VideoBackend.h"
#endif

namespace canvas {

InstanceManager::InstanceManager(EventLoop& loop, Platform* platform,
                                 Config& config, AudioPolicy& audio,
                                 ChildSupervisor& supervisor)
    : loop_(loop),
      platform_(platform),
      config_(config),
      audio_(audio),
      supervisor_(supervisor) {
    supervisor_.onBroken([this](const std::string& tag) { handleBroken(tag); });
    if (platform_) {
        platform_->demoteWindowsOf(
            [this](pid_t pid) { return supervisor_.ownsPid(pid); });
        platform_->setOnPointer(
            [this](const std::string& output, double x, double y) {
                auto it = slots_.find(output);
                if (it != slots_.end() && it->second.instance)
                    it->second.instance->setPointer(x, y);
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

    // Which outputs each output's instance should cover.
    std::map<std::string, std::vector<std::string>> groups;
    for (const auto& out : outputs) {
        const WallpaperInfo* info = desired[out.name];
        if (!info) continue;
        auto& group = groups[out.name];
        if (!mergeable(*info)) {
            group.push_back(out.name);
            continue;
        }
        for (const auto& [name, other] : desired)  // map order: sorted
            if (other && other->id == info->id) group.push_back(name);
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
        startInstance(groups[out.name], *info);
    }
}

bool InstanceManager::mergeable(const WallpaperInfo& info) const {
    return info.type == WallpaperType::Scene && platform_ &&
           platform_->isWayland();
}

void InstanceManager::startInstance(const std::vector<std::string>& group,
                                    const WallpaperInfo& info) {
    // Only merged scenes cover more than one output; everything else
    // (video, web, X11 scenes) targets group.front() alone.
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
        case WallpaperType::Scene: {
            LweOptions opts;
            opts.enginePath = config_.sceneEnginePath;
            opts.wallpaperDir = info.dir.string();
            opts.assetsDir = assetsDir_;
            opts.fps = config_.fps;
            opts.scaling = config_.scaling;
            opts.volume = audio_.volume();
            opts.disableMouse = config_.sceneDisableMouse;
            // Per wallpaper property values: pinned ones always, plus (with
            // scene.randomize_options) a random pick per dropdown, drawn
            // once per launch (each rotation relaunches) and captured below
            // so an audio restart of the same instance keeps the variant.
            static const Config::WallpaperSettings kNoSettings;
            auto ws = config_.wallpapers.find(info.id);
            const auto& settings =
                ws != config_.wallpapers.end() ? ws->second : kNoSettings;
            opts.properties = resolveSceneProperties(
                readUserProperties(info.dir), settings.properties,
                settings.allowed, config_.sceneRandomizeOptions,
                config_.sceneIncludeNsfwOptions, optionRng_);
            for (const auto& [name, value] : opts.properties)
                log::info() << "scene " << info.id << ": " << name << "="
                            << value;
            if (mergeable(info)) {
                opts.screenRoots = group;
                if (sceneLayerSupported_) opts.layer = config_.layer;
            } else
                opts.windowGeometry = geometry;
            // The tag names the first output; handleBroken drops the whole
            // group through it.
            instance = std::make_shared<ExternalProcessInstance>(
                supervisor_, "scene:" + outputName, outputName, info.id,
                muted, [opts](bool m) {
                    LweOptions o = opts;
                    o.muted = m;
                    return LweCommandBuilder::build(o);
                });
            break;
        }
        case WallpaperType::Video: {
#ifdef CANVAS_HAVE_VIDEO
            RenderSurface* surface = surfaceFor(outputName);
            if (!surface) return;
            instance = std::make_shared<VideoInstance>(
                loop_, *surface, info.id, info.filePath().string(),
                config_.scaling, muted, audio_.volume());
            break;
#else
            log::error() << "video backend not built";
            return;
#endif
        }
        case WallpaperType::Web: {
            std::string helper = findWebHelper();
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
    std::string where;
    for (const auto& name : group) where += (where.empty() ? "" : ", ") + name;
    log::info() << "starting " << toString(info.type) << " wallpaper "
                << info.id << " (" << info.title << ") on " << where;
    instance->start();
    for (const auto& name : group) slots_[name] = Slot{instance, group};
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

std::string InstanceManager::findWebHelper() {
    // Next to the daemon binary (build tree and install prefix both work).
    std::error_code ec;
    auto self = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        auto candidate = self.parent_path() / "desktop-canvas-web";
        if (std::filesystem::exists(candidate, ec)) return candidate.string();
    }
    // PATH lookup.
    const char* path = std::getenv("PATH");
    if (path) {
        std::istringstream stream(path);
        std::string dir;
        while (std::getline(stream, dir, ':')) {
            auto candidate = std::filesystem::path(dir) / "desktop-canvas-web";
            if (std::filesystem::exists(candidate, ec))
                return candidate.string();
        }
    }
    return "";
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
    // tag format: "scene:<output>"; for a merged scene, its first output.
    auto pos = tag.find(':');
    if (pos == std::string::npos) return;
    std::string output = tag.substr(pos + 1);
    auto it = slots_.find(output);
    if (it == slots_.end() || !it->second.instance) return;
    std::string id = it->second.instance->wallpaperId();
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
    for (auto* instance : instances()) instance->pause();
}

void InstanceManager::resumeAll() {
    for (auto* instance : instances()) instance->resume();
}

void InstanceManager::setMuted(bool muted) {
    audio_.setDefaultMuted(muted);
    config_.audioMuted = muted;
    config_.save(config_.sourcePath);
    for (auto* instance : instances())
        instance->setMuted(audio_.effectiveMuted(instance->wallpaperId()));
}

std::map<std::string, std::string> InstanceManager::running() const {
    std::map<std::string, std::string> result;
    for (const auto& [name, slot] : slots_)
        if (slot.instance) result[name] = slot.instance->wallpaperId();
    return result;
}

}  // namespace canvas
