// Implementation of X11DesktopOutput declared in X11DesktopOutput.h.
#include "display/x11/X11DesktopOutput.h"

#include <X11/Xatom.h>

#include "core/EventLoop.h"
#include "core/Log.h"
#include "gl/EglContext.h"

namespace canvas {

X11DesktopOutput::X11DesktopOutput(EventLoop& loop, X11Connection& conn,
                                   EglContext& egl, const X11OutputInfo& info,
                                   int fps)
    : loop_(loop),
      conn_(conn),
      egl_(egl),
      name_(info.name),
      width_(info.width),
      height_(info.height),
      frame_interval_ms_(fps > 0 ? 1000 / fps : 33) {
    Display* dpy = conn.display();
    // The window visual must match the EGL config's native visual, or
    // eglCreateWindowSurface fails with EGL_BAD_MATCH.
    XVisualInfo vinfo_template{};
    vinfo_template.visualid = static_cast<VisualID>(egl.nativeVisualId());
    int vinfo_count = 0;
    XVisualInfo* vinfo =
        XGetVisualInfo(dpy, VisualIDMask, &vinfo_template, &vinfo_count);
    Visual* visual = CopyFromParent;
    int depth = CopyFromParent;
    Colormap colormap = 0;
    if (vinfo && vinfo_count > 0) {
        visual = vinfo->visual;
        depth = vinfo->depth;
        colormap = XCreateColormap(dpy, conn.root(), visual, AllocNone);
    }
    XSetWindowAttributes attrs{};
    attrs.background_pixel = BlackPixel(dpy, DefaultScreen(dpy));
    attrs.border_pixel = 0;
    attrs.colormap = colormap;
    attrs.event_mask = StructureNotifyMask;
    unsigned long attr_mask = CWBackPixel | CWBorderPixel | CWEventMask;
    if (colormap) attr_mask |= CWColormap;
    window_ = XCreateWindow(dpy, conn.root(), info.x, info.y,
                            static_cast<unsigned>(info.width),
                            static_cast<unsigned>(info.height), 0, depth,
                            InputOutput, visual, attr_mask, &attrs);
    if (vinfo) XFree(vinfo);

    // Desktop type: EWMH window managers keep it below everything and skip
    // it in taskbars/pagers.
    Atom desktop = conn.atom("_NET_WM_WINDOW_TYPE_DESKTOP");
    XChangeProperty(dpy, window_, conn.atom("_NET_WM_WINDOW_TYPE"), XA_ATOM,
                    32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&desktop), 1);
    Atom states[2] = {conn.atom("_NET_WM_STATE_SKIP_TASKBAR"),
                      conn.atom("_NET_WM_STATE_SKIP_PAGER")};
    XChangeProperty(dpy, window_, conn.atom("_NET_WM_STATE"), XA_ATOM, 32,
                    PropModeReplace,
                    reinterpret_cast<unsigned char*>(states), 2);
    XStoreName(dpy, window_, "desktop-canvas");

    XMapWindow(dpy, window_);
    XLowerWindow(dpy, window_);
    XFlush(dpy);

    conn.addWindowHandler(window_, [this](const XEvent& ev) {
        if (ev.type == ConfigureNotify) {
            width_ = ev.xconfigure.width;
            height_ = ev.xconfigure.height;
            if (on_ready_) on_ready_();
        }
    });

    egl_surface_ = egl_.createWindowSurface(
        reinterpret_cast<void*>(window_));
    log::info() << "x11 desktop window on " << name_ << " " << width_ << "x"
                << height_ << " at " << info.x << "," << info.y;
}

X11DesktopOutput::~X11DesktopOutput() {
    if (frame_timer_) loop_.cancelTimer(frame_timer_);
    conn_.removeWindowHandler(window_);
    if (egl_surface_ != EGL_NO_SURFACE) egl_.destroySurface(egl_surface_);
    if (window_) XDestroyWindow(conn_.display(), window_);
    conn_.flush();
}

bool X11DesktopOutput::makeCurrent() {
    return ready() && egl_.makeCurrent(egl_surface_);
}

void X11DesktopOutput::swapBuffers() { egl_.swapBuffers(egl_surface_); }

void X11DesktopOutput::requestFrame(std::function<void()> cb) {
    if (frame_timer_) loop_.cancelTimer(frame_timer_);
    frame_timer_ = loop_.addTimer(frame_interval_ms_, [this, cb = std::move(cb)] {
        frame_timer_ = 0;
        cb();
    });
}

void X11DesktopOutput::setOnReady(std::function<void()> cb) {
    on_ready_ = std::move(cb);
    // The window exists immediately on X11; report readiness right away so
    // backends start without waiting for an event.
    if (on_ready_ && ready()) on_ready_();
}

}  // namespace canvas
