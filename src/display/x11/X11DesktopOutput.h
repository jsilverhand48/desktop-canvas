// RenderSurface implementation on an X11 DESKTOP type window.
//
// One undecorated window per CRTC, stamped _NET_WM_WINDOW_TYPE_DESKTOP so
// EWMH window managers keep it below normal windows and docks stay above
// it, and lowered explicitly for WMs that need the hint. Frame pacing has
// no compositor callback on X11, so requestFrame arms an event loop timer
// for the next slot of the configured fps, on absolute deadlines (no drift;
// after falling behind it restarts from now instead of bursting).
//
// The window outlives individual wallpapers (see RenderSurface.h): hide()
// unmaps it and cancels the frame timer, show() maps and lowers it again.
// The EGLSurface stays valid while unmapped, so ready() is unaffected.
//
// With `window` set it is instead an ordinary titled window named "window"
// (desktop-canvas-scene --window): normal type, not lowered, and closing it
// (WM_DELETE_WINDOW) makes closed() true and runs the close callback.
#pragma once

#include <X11/Xlib.h>

#include <EGL/egl.h>

#include <chrono>
#include <functional>
#include <string>

#include "display/RenderSurface.h"
#include "display/x11/X11Connection.h"

namespace canvas {

class EglContext;
class EventLoop;

class X11DesktopOutput : public RenderSurface {
public:
    X11DesktopOutput(EventLoop& loop, X11Connection& conn, EglContext& egl,
                     const X11OutputInfo& info, int fps, bool window = false,
                     std::function<void()> onClose = {});
    ~X11DesktopOutput() override;

    const std::string& name() const override { return name_; }
    bool ready() const override { return egl_surface_ != EGL_NO_SURFACE; }
    int pixelWidth() const override { return width_; }
    int pixelHeight() const override { return height_; }
    bool makeCurrent() override;
    void swapBuffers() override;
    void requestFrame(std::function<void()> cb) override;
    void setOnReady(std::function<void()> cb) override;
    void show() override;
    void hide() override;
    bool closed() const override { return closed_; }

private:
    EventLoop& loop_;
    X11Connection& conn_;
    EglContext& egl_;
    std::string name_;
    int width_, height_;
    std::chrono::nanoseconds frame_period_;
    std::chrono::steady_clock::time_point next_frame_{};
    Window window_ = 0;
    EGLSurface egl_surface_ = EGL_NO_SURFACE;
    uint64_t frame_timer_ = 0;
    bool shown_ = true;  // constructor maps the window
    bool window_mode_ = false;
    bool closed_ = false;
    std::function<void()> on_ready_;
    std::function<void()> on_close_;
};

}  // namespace canvas
