// Implementation of X11Connection declared in X11Connection.h.
#include "display/x11/X11Connection.h"

#include <X11/Xatom.h>
#include <sys/epoll.h>

#include <algorithm>
#include <cstring>

#include "core/EventLoop.h"
#include "core/Log.h"

namespace canvas {

X11Connection::~X11Connection() {
    if (display_) XCloseDisplay(display_);
}

bool X11Connection::connect(EventLoop& loop) {
    display_ = XOpenDisplay(nullptr);
    if (!display_) {
        log::error() << "XOpenDisplay failed";
        return false;
    }
    root_ = DefaultRootWindow(display_);

    int randr_error_base = 0;
    if (!XRRQueryExtension(display_, &randr_event_base_, &randr_error_base)) {
        log::error() << "XRandR extension missing";
        return false;
    }
    XRRSelectInput(display_, root_, RRScreenChangeNotifyMask);
    // SubstructureNotify lets us see MapNotify for new top level windows
    // (used by demoteWindowsOf for scene children).
    XSelectInput(display_, root_, SubstructureNotifyMask);
    refreshOutputs();

    loop.addFd(ConnectionNumber(display_), EPOLLIN, [this](uint32_t) {
        while (XPending(display_)) {
            XEvent ev;
            XNextEvent(display_, &ev);
            handleEvent(ev);
        }
        XFlush(display_);
    });
    return true;
}

void X11Connection::refreshOutputs() {
    outputs_.clear();
    XRRScreenResources* res = XRRGetScreenResourcesCurrent(display_, root_);
    if (!res) return;
    for (int i = 0; i < res->noutput; i++) {
        XRROutputInfo* out = XRRGetOutputInfo(display_, res, res->outputs[i]);
        if (!out) continue;
        if (out->connection == RR_Connected && out->crtc) {
            XRRCrtcInfo* crtc = XRRGetCrtcInfo(display_, res, out->crtc);
            if (crtc && crtc->width > 0) {
                outputs_.push_back(X11OutputInfo{
                    out->name, static_cast<int>(crtc->x),
                    static_cast<int>(crtc->y), static_cast<int>(crtc->width),
                    static_cast<int>(crtc->height)});
            }
            if (crtc) XRRFreeCrtcInfo(crtc);
        }
        XRRFreeOutputInfo(out);
    }
    XRRFreeScreenResources(res);
    log::info() << "x11: " << outputs_.size() << " connected output(s)";
}

void X11Connection::handleEvent(const XEvent& ev) {
    if (ev.type == randr_event_base_ + RRScreenChangeNotify) {
        XRRUpdateConfiguration(const_cast<XEvent*>(&ev));
        refreshOutputs();
        if (outputs_changed_cb_) outputs_changed_cb_();
        return;
    }
    if (ev.type == MapNotify && demote_predicate_) {
        pid_t pid = windowPid(ev.xmap.window);
        if (pid > 0 && demote_predicate_(pid)) demote(ev.xmap.window);
    }
    for (auto& [win, cb] : window_handlers_) {
        if (ev.xany.window == win) {
            cb(ev);
            break;
        }
    }
}

pid_t X11Connection::windowPid(Window w) {
    Atom type;
    int format;
    unsigned long items, bytes;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(display_, w, atom("_NET_WM_PID"), 0, 1, False,
                           XA_CARDINAL, &type, &format, &items, &bytes,
                           &data) != Success ||
        !data)
        return -1;
    pid_t pid = static_cast<pid_t>(*reinterpret_cast<unsigned long*>(data));
    XFree(data);
    return pid;
}

void X11Connection::demote(Window w) {
    log::info() << "x11: demoting window " << w << " to desktop layer";
    Atom desktop = atom("_NET_WM_WINDOW_TYPE_DESKTOP");
    XChangeProperty(display_, w, atom("_NET_WM_WINDOW_TYPE"), XA_ATOM, 32,
                    PropModeReplace,
                    reinterpret_cast<unsigned char*>(&desktop), 1);
    XLowerWindow(display_, w);
    XFlush(display_);
}

void X11Connection::addWindowHandler(Window w,
                                     std::function<void(const XEvent&)> cb) {
    window_handlers_.emplace_back(w, std::move(cb));
}

void X11Connection::removeWindowHandler(Window w) {
    window_handlers_.erase(
        std::remove_if(window_handlers_.begin(), window_handlers_.end(),
                       [w](const auto& p) { return p.first == w; }),
        window_handlers_.end());
}

Atom X11Connection::atom(const char* name) {
    return XInternAtom(display_, name, False);
}

void X11Connection::flush() {
    if (display_) XFlush(display_);
}

}  // namespace canvas
