// Per (output, wallpaper) lifecycle unit.
//
// Implementations:
//   ExternalProcessInstance (src/process/): web wallpapers rendered by the
//     desktop-canvas-web helper.
//   SceneHelperInstance (src/process/): scene wallpapers rendered by
//     desktop-canvas-scene, the project's own helper, driven live over its
//     control channel (process/HelperProtocol.h).
//   VideoInstance (src/render/VideoBackend.h): video wallpapers rendered in
//     process with libmpv into the output's GL surface.
// InstanceManager owns instances and translates config/IPC changes into
// start/stop/pause/setMuted/setVolume calls. One instance may cover several
// outputs (a merged scene group); outputName() is the first of them.
#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace canvas {

class WallpaperInstance {
public:
    virtual ~WallpaperInstance() = default;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void pause() {}
    virtual void resume() {}

    // May restart the instance when the backend cannot change mute live
    // (the web helper takes it on its command line).
    virtual void setMuted(bool muted) = 0;
    // True when setMuted applies without restarting anything; muting while
    // other applications play (src/scene/audio/README.md section 6) only
    // touches these.
    virtual bool mutesLive() const { return true; }

    // What media players play (src/scene/audio/README.md section 7), the
    // body of a "media" message; only native scene helpers use it.
    virtual void setMedia(const nlohmann::json& /*media*/) {}

    // Applies a new volume (0..100) live and returns true, or returns false
    // when the backend can only take it at launch; the owner then restarts
    // the instance.
    virtual bool setVolume(int /*percent*/) { return false; }

    // Pointer position on output, in output local logical coordinates.
    // Backends that do not use input ignore it.
    virtual void setPointer(const std::string& /*output*/, double /*x*/,
                            double /*y*/) {}

    // A fullscreen application covers (or uncovers) one of this instance's
    // outputs. InstanceManager already pauses an instance whose outputs are
    // all covered; backends that can skip single outputs (the native
    // helper) use this for partly covered groups. Default: ignored.
    virtual void setCovered(const std::string& /*output*/, bool /*covered*/) {}

    virtual const std::string& wallpaperId() const = 0;
    virtual const std::string& outputName() const = 0;
};

}  // namespace canvas
