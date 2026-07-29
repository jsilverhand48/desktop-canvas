// Display platform abstraction.
//
// A Platform enumerates outputs (monitors) and creates RenderSurfaces for in
// process backends. Two implementations exist: WaylandPlatform (layer shell)
// and X11Platform (DESKTOP type windows). main.cpp picks one based on
// WAYLAND_DISPLAY/DISPLAY. Scene wallpapers do not use RenderSurfaces; the
// linux-wallpaperengine child creates its own surface, so it only needs the
// output name (Wayland) or geometry (X11) exposed here.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "display/RenderSurface.h"

namespace canvas {

class EventLoop;

struct OutputDesc {
    std::string name;      // connector name, e.g. "DP-1"
    int width = 0;         // logical size
    int height = 0;
    int x = 0;             // position in the global layout (X11 geometry)
    int y = 0;
};

class Platform {
public:
    virtual ~Platform() = default;

    virtual bool init(EventLoop& loop) = 0;
    virtual std::vector<OutputDesc> outputs() const = 0;
    virtual std::unique_ptr<RenderSurface> createSurface(
        const std::string& outputName, const std::string& layer) = 0;
    virtual void setOnOutputsChanged(std::function<void()> cb) = 0;

    // True on Wayland; ExternalProcessInstance uses this to choose between
    // --screen-root and --window for linux-wallpaperengine.
    virtual bool isWayland() const = 0;

    // X11 only: newly mapped windows whose _NET_WM_PID satisfies the
    // predicate are stamped DESKTOP type and lowered (pushes scene children
    // running in --window mode behind everything). No-op on Wayland where
    // lwe creates proper layer surfaces itself.
    virtual void demoteWindowsOf(std::function<bool(pid_t)> /*predicate*/) {}

    // Pointer position over the wallpaper, in output local logical
    // coordinates. Wayland delivers this only while no other surface is
    // above ours under the cursor; X11 polls the global pointer and always
    // delivers. Consumed by backends that support mouse (web).
    using PointerCallback =
        std::function<void(const std::string& output, double x, double y)>;
    virtual void setOnPointer(PointerCallback /*cb*/) {}
};

}  // namespace canvas
