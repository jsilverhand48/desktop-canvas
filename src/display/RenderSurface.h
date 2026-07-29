// Abstract GPU surface an in-process backend renders into.
//
// Implemented by WaylandLayerOutput (layer shell surface + wl_egl_window)
// and, on X11, by X11DesktopOutput (DESKTOP type window + EGL). Backends
// (VideoBackend, WebBackend) only see this interface, which keeps them
// platform independent.
//
// Frame pacing contract: call requestFrame(cb) after finishing a swap; cb
// fires when the compositor is ready for the next frame (Wayland frame
// callback) or after a frame timer (X11). Backends must not swap again
// before the callback to avoid blocking the event loop on an occluded
// surface.
#pragma once

#include <functional>
#include <string>

namespace canvas {

class RenderSurface {
public:
    virtual ~RenderSurface() = default;

    // Output connector name ("DP-1", "HDMI-A-1").
    virtual const std::string& name() const = 0;

    // True once the surface is configured and the EGL surface exists.
    virtual bool ready() const = 0;

    // Buffer size in pixels (logical size times scale).
    virtual int pixelWidth() const = 0;
    virtual int pixelHeight() const = 0;

    virtual bool makeCurrent() = 0;
    virtual void swapBuffers() = 0;

    // One shot next-frame notification; see pacing contract above.
    virtual void requestFrame(std::function<void()> cb) = 0;

    // Invoked when the surface becomes ready or is resized. The backend
    // should (re)start rendering when this fires.
    virtual void setOnReady(std::function<void()> cb) = 0;
};

}  // namespace canvas
