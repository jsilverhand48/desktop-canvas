// X11 display connection, XRandR output enumeration, and window helpers.
//
// Outputs are CRTCs with connected outputs; their connector names come from
// XRandR so they match the names used on Wayland ("DP-1" etc). The
// connection watches RRScreenChangeNotify for monitor hotplug and
// SubstructureNotify on the root window so newly mapped windows of
// supervised scene children can be demoted to desktop windows (see
// demoteWindowsOf).
#pragma once

#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

#include <functional>
#include <string>
#include <vector>

namespace canvas {

class EventLoop;

struct X11OutputInfo {
    std::string name;
    int x = 0, y = 0;
    int width = 0, height = 0;
};

class X11Connection {
public:
    ~X11Connection();

    bool connect(EventLoop& loop);
    Display* display() const { return display_; }
    Window root() const { return root_; }

    const std::vector<X11OutputInfo>& outputs() const { return outputs_; }
    void setOnOutputsChanged(std::function<void()> cb) {
        outputs_changed_cb_ = std::move(cb);
    }

    // Per window event dispatch for X11DesktopOutput instances.
    void addWindowHandler(Window w, std::function<void(const XEvent&)> cb);
    void removeWindowHandler(Window w);

    // When a new top level window whose _NET_WM_PID satisfies the predicate
    // is mapped, stamp it _NET_WM_WINDOW_TYPE_DESKTOP and lower it. Used to
    // push linux-wallpaperengine's --window mode windows behind everything.
    void demoteWindowsOf(std::function<bool(pid_t)> predicate) {
        demote_predicate_ = std::move(predicate);
    }

    Atom atom(const char* name);
    void flush();

private:
    void refreshOutputs();
    void handleEvent(const XEvent& ev);
    pid_t windowPid(Window w);
    void demote(Window w);

    Display* display_ = nullptr;
    Window root_ = 0;
    int randr_event_base_ = 0;
    std::vector<X11OutputInfo> outputs_;
    std::function<void()> outputs_changed_cb_;
    std::function<bool(pid_t)> demote_predicate_;
    std::vector<std::pair<Window, std::function<void(const XEvent&)>>>
        window_handlers_;
};

}  // namespace canvas
