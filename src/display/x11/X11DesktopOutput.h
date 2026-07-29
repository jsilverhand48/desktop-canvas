// RenderSurface implementation on an X11 DESKTOP type window.
//
// One undecorated window per CRTC, stamped _NET_WM_WINDOW_TYPE_DESKTOP so
// EWMH window managers keep it below normal windows and docks stay above
// it, and lowered explicitly for WMs that need the hint. Frame pacing has
// no compositor callback on X11, so requestFrame arms an event loop timer
// at the configured fps.
#pragma once

#include <X11/Xlib.h>

#include <EGL/egl.h>

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
                     const X11OutputInfo& info, int fps);
    ~X11DesktopOutput() override;

    const std::string& name() const override { return name_; }
    bool ready() const override { return egl_surface_ != EGL_NO_SURFACE; }
    int pixelWidth() const override { return width_; }
    int pixelHeight() const override { return height_; }
    bool makeCurrent() override;
    void swapBuffers() override;
    void requestFrame(std::function<void()> cb) override;
    void setOnReady(std::function<void()> cb) override;

private:
    EventLoop& loop_;
    X11Connection& conn_;
    EglContext& egl_;
    std::string name_;
    int width_, height_;
    int frame_interval_ms_;
    Window window_ = 0;
    EGLSurface egl_surface_ = EGL_NO_SURFACE;
    uint64_t frame_timer_ = 0;
    std::function<void()> on_ready_;
};

}  // namespace canvas
