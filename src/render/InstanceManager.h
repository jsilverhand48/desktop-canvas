// Orchestrates wallpaper instances across outputs.
//
// Owns the scan results, translates config assignments (output name to
// wallpaper id) into running instances, and services IPC commands. Per
// wallpaper type:
//   scene -> by sceneEngineFor(): SceneHelperInstance (desktop-canvas-scene,
//            engines "native" and "auto") or ExternalProcessInstance
//            (linux-wallpaperengine child, engine "lwe" and every fallback)
//   video -> whatever the injected VideoFactory builds on a Platform
//            RenderSurface (main.cpp installs the libmpv VideoInstance;
//            without a factory video wallpapers are logged and left blank)
//   web   -> ExternalProcessInstance running desktop-canvas-web when built
// This module links no display or video library: Platform and RenderSurface
// are interfaces and video arrives through the factory, so tests can drive
// apply() against fakes.
// Wallpapers whose scene child keeps crashing are added to config.broken and
// unassigned automatically.
//
// Outputs showing the same scene wallpaper share one process (one
// --screen-root / --output per output), so assets and the GL context are
// loaded once instead of per screen: on Wayland for lwe, everywhere for the
// native helper. Changing any output of such a group restarts the whole
// group with the new split.
//
// Engine "auto" fallback: a strict native helper that reports unsupported
// content (exit 3), or keeps crashing, gets a NativeSupportCache verdict;
// the wallpaper is relaunched and now resolves to lwe until the content or
// the helper binary changes.
//
// Video RenderSurfaces are kept per output, created on the first video for
// that output and reused (hidden in between) until the output disappears or
// the surface reports closed(). Wayland layer surfaces report closed() after
// one use (KWin will not remap them), so on Wayland each video gets a fresh
// surface; X11 windows are reused.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "config/Config.h"
#include "display/Platform.h"
#include "render/NativeSupportCache.h"
#include "source/WallpaperInfo.h"

namespace canvas {

class AudioPolicy;
class SpectrumService;
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

    // Re-runs the workshop scanner over the configured/auto roots, bumps
    // libraryRevision() and calls the onRescan listener (main.cpp points the
    // LibraryWatcher at roots() there, so changed content_roots are
    // followed too).
    void rescan();
    const std::vector<std::filesystem::path>& roots() const { return roots_; }
    // Increments on every rescan; the settings UI reloads its library grid
    // when the value it sees in /api/status changes.
    uint64_t libraryRevision() const { return libraryRevision_; }
    void setOnRescan(std::function<void()> cb) { onRescan_ = std::move(cb); }
    const std::vector<WallpaperInfo>& items() const { return items_; }
    const WallpaperInfo* find(const std::string& id) const;

    // True when id is present in the library, of a renderable type, and not
    // on the broken blacklist. Rotator uses this to skip dead playlist
    // entries instead of assigning them and blanking an output.
    bool usable(const std::string& id) const;

    // Which renderer a scene wallpaper gets (config "scene.engine" or the
    // wallpaper's own "engine"): Lwe for "lwe", Native for "native",
    // NativeStrict for "auto" unless a still valid NativeSupportCache verdict
    // says the native helper cannot show it (then Lwe). Native and
    // NativeStrict fall back to Lwe when desktop-canvas-scene is not found.
    enum class SceneEngine { Lwe, Native, NativeStrict };
    SceneEngine sceneEngineFor(const WallpaperInfo& info) const;
    // Path of desktop-canvas-scene (HelperLocator), empty when missing.
    const std::string& sceneHelperPath() const { return sceneHelper_; }
    NativeSupportCache& nativeSupport() { return *nativeSupport_; }
    const NativeSupportCache& nativeSupport() const { return *nativeSupport_; }
    // Points the verdict cache at another file and loads it (tests).
    void useNativeSupportCache(const std::filesystem::path& file);

    // Builds the in-process instance for a video wallpaper on surface (one
    // output; video never merges). Returning null leaves the output blank.
    // Set once at startup; instances already running are unaffected.
    using VideoFactory = std::function<std::shared_ptr<WallpaperInstance>(
        RenderSurface& surface, const WallpaperInfo& info, bool muted)>;
    void setVideoFactory(VideoFactory factory) {
        videoFactory_ = std::move(factory);
    }

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
    // Stops every running instance and re-applies, so settings read at
    // launch (fps, scaling, layer, engine/assets paths) take effect.
    void restartAll();
    // Stops the instances showing id and re-applies: a scene relaunches
    // with freshly drawn random options and its current pinned values.
    void restartWallpaper(const std::string& id);
    // Removes id from the broken blacklist (persisted) and re-applies.
    void clearBroken(const std::string& id);
    // Pause state is remembered: instances started while paused (rotation,
    // hotplug, a fallback relaunch) start paused too.
    void pauseAll();
    void resumeAll();
    bool paused() const { return paused_; }
    void setMuted(bool muted);  // global default; propagates to instances
    // Applies a new volume to every instance: live where the backend can
    // (video, native scenes), otherwise by restarting unmuted instances.
    void setVolume(int volume);
    // The audio capture shared with native scene helpers (set once at
    // startup, must outlive this manager; null: helpers read silence).
    // Helpers started afterwards get its memory and report their demand.
    void setSpectrumService(SpectrumService* service) { spectrum_ = service; }
    // Config audio.processing, applied live.
    void setAudioProcessing(bool enabled);
    // Muting while other applications play (src/scene/audio/README.md
    // section 6): whether another application plays now (the watcher's
    // verdict, after its hold time) and config audio.auto_mute. Applies
    // only to instances that mute live (native helpers, videos); lwe mutes
    // itself.
    void setOthersPlaying(bool playing);
    void setAutoMute(bool enabled);
    bool autoMuted() const { return autoMute_ && othersPlaying_; }

    // Fullscreen pause. setToplevels stores the desktop's latest window list
    // (Platform::watchToplevels or the KDE bridge) and recomputes the
    // covered outputs with the config rules (desktop/FullscreenPolicy.h);
    // refreshCoverage() recomputes after the rules change. An instance whose
    // outputs are all covered pauses exactly like a manual pause and resumes
    // when uncovered (unless manually paused); every instance also hears
    // setCovered() per output.
    void setToplevels(std::vector<ToplevelInfo> windows);
    void refreshCoverage();
    const std::set<std::string>& coveredOutputs() const { return covered_; }
    // Global logical cursor position (the KDE bridge reports it): routed to
    // the instance on the output under it, in output local coordinates.
    void pointerAtGlobal(double x, double y);

    // Status for IPC: output -> wallpaper id currently running.
    std::map<std::string, std::string> running() const;

private:
    struct Slot {
        // Shared by every output of a merged scene group.
        std::shared_ptr<WallpaperInstance> instance;
        // Sorted outputs the instance covers; just this output when unmerged.
        std::vector<std::string> group;
    };

    // True when outputs showing info can share one process: native scenes
    // everywhere, lwe scenes on Wayland only (lwe's X11 mode is a window).
    bool mergeable(const WallpaperInfo& info) const;
    // Starts the scene in group with the native helper or lwe.
    std::shared_ptr<WallpaperInstance> makeSceneInstance(
        const std::vector<std::string>& group, const WallpaperInfo& info,
        bool muted, const std::string& geometry, bool span);
    // A strict native helper could not show id: remember it and relaunch
    // the wallpaper (now resolving to lwe).
    void handleUnsupported(const std::string& id,
                           const std::vector<std::string>& features);
    // Stamp of the file whose change should retry the native helper.
    static std::string contentStamp(const WallpaperInfo& info);
    // Whether lwe accepts --layer; probes `lwe --help` once, lazily, the
    // first time an lwe scene starts (re-probed after restartAll()).
    bool lweLayerSupported();
    // Starts one instance covering every output in group (size 1 unless
    // mergeable) and records a slot for each of them.
    // span: group is a configured span (one canvas over all of it).
    void startInstance(const std::vector<std::string>& group,
                       const WallpaperInfo& info, bool span = false);
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
    // Pauses or resumes each instance to match paused_ or full coverage of
    // its outputs; transitions only, tracked in pauseApplied_.
    void updatePause();
    // A supervised child failed too often. lwe and web: blacklist the
    // wallpaper (config "broken"). Native helper under "auto": record a
    // "crash" verdict and relaunch with lwe instead.
    void handleBroken(const std::string& tag);
    bool isBroken(const std::string& id) const;

    EventLoop& loop_;
    Platform* platform_;  // may be null in --list only mode
    Config& config_;
    AudioPolicy& audio_;
    ChildSupervisor& supervisor_;
    std::vector<WallpaperInfo> items_;
    std::vector<std::filesystem::path> roots_;
    uint64_t libraryRevision_ = 0;
    std::function<void()> onRescan_;
    std::string assetsDir_;
    VideoFactory videoFactory_;
    SpectrumService* spectrum_ = nullptr;
    bool autoMute_ = true;
    bool othersPlaying_ = false;
    void applyAutoMute();
    std::optional<bool> lweLayer_;  // lazy `lwe --help` probe result
    std::string sceneHelper_;       // desktop-canvas-scene, empty if missing
    std::unique_ptr<NativeSupportCache> nativeSupport_;
    bool paused_ = false;
    std::vector<ToplevelInfo> toplevels_;
    std::set<std::string> covered_;
    std::map<const WallpaperInstance*, bool> pauseApplied_;
    // Draws random scene variants when config_.sceneRandomizeOptions is on.
    std::mt19937 optionRng_{std::random_device{}()};
    // Declared before slots_ so instances, which borrow these surfaces, are
    // destroyed first.
    std::map<std::string, std::unique_ptr<RenderSurface>> surfaces_;
    std::map<std::string, Slot> slots_;      // key: output name
    std::map<std::string, std::string> overrides_;  // rotation, not persisted
};

}  // namespace canvas
