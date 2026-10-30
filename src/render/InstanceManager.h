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
//
// On Wayland, outputs showing the same scene wallpaper share one
// linux-wallpaperengine process (one --screen-root per output), so assets
// and the GL context are loaded once instead of per screen. Changing any
// output of such a group restarts the whole group with the new split.
//
// Video RenderSurfaces are kept per output, created on the first video for
// that output and reused (hidden in between) until the output disappears or
// the surface reports closed(). Wayland layer surfaces report closed() after
// one use (KWin will not remap them), so on Wayland each video gets a fresh
// surface; X11 windows are reused.
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

    // Whether the scene engine accepts --layer (probed once at startup).
    // When true, Wayland scenes are put on config.layer like video surfaces;
    // otherwise lwe picks its own default ("bottom" on r598+, which covers
    // the Plasma desktop icons). Takes effect on the next started scene.
    void setSceneLayerSupported(bool supported) { sceneLayerSupported_ = supported; }

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
        // Shared by every output of a merged scene group.
        std::shared_ptr<WallpaperInstance> instance;
        // Sorted outputs the instance covers; just this output when unmerged.
        std::vector<std::string> group;
    };

    // True when outputs showing info can share one process (Wayland scenes).
    bool mergeable(const WallpaperInfo& info) const;
    // Starts one instance covering every output in group (size 1 unless
    // mergeable) and records a slot for each of them.
    void startInstance(const std::vector<std::string>& group,
                       const WallpaperInfo& info);
    // Surface for outputName, created on first use or when the previous one
    // reports closed() (compositor closed it, or a used Wayland surface).
    // Null on failure.
    RenderSurface* surfaceFor(const std::string& outputName);
    // Stops the instance on outputName and drops the slot of every output
    // sharing it.
    void stopInstance(const std::string& outputName);
    // Each running instance once, even when shared by several outputs, so
    // pause/resume/mute act (and mute restarts) a merged process only once.
    std::vector<WallpaperInstance*> instances() const;
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
    bool sceneLayerSupported_ = false;
    // Declared before slots_ so instances, which borrow these surfaces, are
    // destroyed first.
    std::map<std::string, std::unique_ptr<RenderSurface>> surfaces_;
    std::map<std::string, Slot> slots_;      // key: output name
    std::map<std::string, std::string> overrides_;  // rotation, not persisted
};

}  // namespace canvas
