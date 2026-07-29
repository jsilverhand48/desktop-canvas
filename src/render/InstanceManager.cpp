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
    items_ = WorkshopScanner::scan(roots);
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

    // Drop slots for outputs that disappeared.
    std::vector<std::string> stale;
    for (const auto& [name, slot] : slots_) {
        bool exists = std::any_of(outputs.begin(), outputs.end(),
                                  [&](const auto& o) { return o.name == name; });
        if (!exists) stale.push_back(name);
    }
    for (const auto& name : stale) stopInstance(name);

    for (const auto& out : outputs) {
        std::string id = resolve(out.name);
        const WallpaperInfo* info = id.empty() ? nullptr : find(id);
        if (info && (info->type == WallpaperType::Unsupported || isBroken(id))) {
            log::warn() << "wallpaper " << id << " not usable on " << out.name
                        << (isBroken(id) ? " (marked broken)"
                                         : " (" + info->unsupportedReason + ")");
            info = nullptr;
        }
        auto it = slots_.find(out.name);
        const std::string current =
            it != slots_.end() && it->second.instance
                ? it->second.instance->wallpaperId()
                : "";
        if (!info) {
            if (!current.empty()) stopInstance(out.name);
            continue;
        }
        if (current == info->id) continue;
        stopInstance(out.name);
        startInstance(out.name, *info);
    }
}

void InstanceManager::startInstance(const std::string& outputName,
                                    const WallpaperInfo& info) {
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
    Slot slot;
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
            if (platform_ && platform_->isWayland())
                opts.screenRoot = outputName;
            else
                opts.windowGeometry = geometry;
            slot.instance = std::make_unique<ExternalProcessInstance>(
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
            slot.surface =
                platform_->createSurface(outputName, config_.layer);
            if (!slot.surface) return;
            slot.instance = std::make_unique<VideoInstance>(
                loop_, *slot.surface, info.id, info.filePath().string(),
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
            slot.instance = std::make_unique<ExternalProcessInstance>(
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
    log::info() << "starting " << toString(info.type) << " wallpaper "
                << info.id << " (" << info.title << ") on " << outputName;
    slot.instance->start();
    slots_[outputName] = std::move(slot);
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
    if (it->second.instance) it->second.instance->stop();
    slots_.erase(it);
}

void InstanceManager::handleBroken(const std::string& tag) {
    // tag format: "scene:<output>"
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
    slots_.erase(it);
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

void InstanceManager::pauseAll() {
    for (auto& [name, slot] : slots_)
        if (slot.instance) slot.instance->pause();
}

void InstanceManager::resumeAll() {
    for (auto& [name, slot] : slots_)
        if (slot.instance) slot.instance->resume();
}

void InstanceManager::setMuted(bool muted) {
    audio_.setDefaultMuted(muted);
    config_.audioMuted = muted;
    config_.save(config_.sourcePath);
    for (auto& [name, slot] : slots_)
        if (slot.instance)
            slot.instance->setMuted(
                audio_.effectiveMuted(slot.instance->wallpaperId()));
}

std::map<std::string, std::string> InstanceManager::running() const {
    std::map<std::string, std::string> result;
    for (const auto& [name, slot] : slots_)
        if (slot.instance) result[name] = slot.instance->wallpaperId();
    return result;
}

}  // namespace canvas
