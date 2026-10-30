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
//
// Lifetime: InstanceManager keeps one surface per output and reuses it across
// wallpaper changes while closed() is false. Backends call show() when they
// start and hide() when they stop; hide() also drops any pending frame and
// ready callbacks, since they capture the backend that is going away. X11
// windows are reused indefinitely. Wayland layer surfaces report closed()
// after their first hide(), because KWin closes a remapped layer surface, so
// every video on Wayland gets a fresh surface.
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

    // Map/unmap the surface without destroying it. show() may make the
    // surface not ready() until the compositor reconfigures it; the ready
    // callback fires once it is. Both are no-ops when already in that state.
    virtual void show() = 0;
    virtual void hide() = 0;

    // True when the surface must not be reused (the compositor closed it, or
    // the platform cannot remap it after hide()); the owner must create a
    // new one.
    virtual bool closed() const { return false; }
};

}  // namespace canvas
