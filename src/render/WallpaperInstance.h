// Per (output, wallpaper) lifecycle unit.
//
// Implementations:
//   ExternalProcessInstance (src/process/): scene wallpapers rendered by a
//     supervised linux-wallpaperengine child process.
//   VideoInstance (src/render/VideoBackend.h): video wallpapers rendered in
//     process with libmpv into the output's GL surface.
//   WebInstance (src/render/web/, optional build): web wallpapers.
// InstanceManager owns instances and translates config/IPC changes into
// start/stop/setMuted calls.
#pragma once

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
    // (scene wallpapers use the --silent flag of their child process).
    virtual void setMuted(bool muted) = 0;

    // Pointer position in output local logical coordinates. Backends that do
    // not use input ignore it.
    virtual void setPointer(double /*x*/, double /*y*/) {}

    virtual const std::string& wallpaperId() const = 0;
    virtual const std::string& outputName() const = 0;
};

}  // namespace canvas
