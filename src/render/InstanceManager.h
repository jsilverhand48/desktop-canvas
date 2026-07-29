// Orchestrates wallpaper instances across outputs.
//
// Owns the scan results, translates config assignments (output name to
// wallpaper id) into running instances, and services IPC commands. Per
// wallpaper type:
//   scene -> ExternalProcessInstance (linux-wallpaperengine child)
//   video -> VideoInstance on a Platform RenderSurface
//   web   -> optional backend (M6); reported unsupported otherwise
// Wallpapers whose scene child keeps crashing are added to config.broken and
// unassigned automatically.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "config/Config.h"
#include "source/WallpaperInfo.h"

namespace canvas {

class AudioPolicy;
class ChildSupervisor;
class EventLoop;
class Platform;
class RenderSurface;
class WallpaperInstance;

class InstanceManager {
public:
    InstanceManager(EventLoop& loop, Platform* platform, Config& config,
                    AudioPolicy& audio, ChildSupervisor& supervisor);
    ~InstanceManager();

    // Re-runs the workshop scanner over the configured/auto roots.
    void rescan();
    const std::vector<WallpaperInfo>& items() const { return items_; }
    const WallpaperInfo* find(const std::string& id) const;

    // True when id is present in the library, of a renderable type, and not
    // on the broken blacklist. Rotator uses this to skip dead playlist
    // entries instead of assigning them and blanking an output.
    bool usable(const std::string& id) const;

    // Reconciles running instances with config assignments and the current
    // output list. Safe to call repeatedly; only differences cause changes.
    void apply();

    // IPC operations. setAssignment persists to the config file.
    bool setAssignment(const std::string& output, const std::string& id,
                       std::string& error);

    // Runtime overrides, used by Rotator. These sit above config assignments
    // when resolving what an output should show and are never persisted, so
    // a rotation cannot overwrite the user's own choices in config.json.
    // Key "*" covers every output; an exact output name beats it.
    void setOverride(const std::string& output, const std::string& id);
    void clearOverrides();
    // Resolved id for an output: override (exact, then "*"), else config.
    std::string resolve(const std::string& output) const;
    void pauseAll();
    void resumeAll();
    void setMuted(bool muted);  // global default; propagates to instances

    // Status for IPC: output -> wallpaper id currently running.
    std::map<std::string, std::string> running() const;

private:
    struct Slot {
        std::unique_ptr<RenderSurface> surface;   // null for scene instances
        std::unique_ptr<WallpaperInstance> instance;
    };

    void startInstance(const std::string& outputName, const WallpaperInfo& info);
    void stopInstance(const std::string& outputName);
    // Locates the desktop-canvas-web helper: next to the daemon binary
    // first, then PATH. Empty when unavailable.
    static std::string findWebHelper();
    void handleBroken(const std::string& tag);
    bool isBroken(const std::string& id) const;

    EventLoop& loop_;
    Platform* platform_;  // may be null in --list only mode
    Config& config_;
    AudioPolicy& audio_;
    ChildSupervisor& supervisor_;
    std::vector<WallpaperInfo> items_;
    std::string assetsDir_;
    std::map<std::string, Slot> slots_;      // key: output name
    std::map<std::string, std::string> overrides_;  // rotation, not persisted
};

}  // namespace canvas
