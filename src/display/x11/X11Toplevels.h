// Other clients' toplevel windows on X11, through EWMH properties.
//
// The window manager lists managed clients in the root window's
// _NET_CLIENT_LIST and the focused one in _NET_ACTIVE_WINDOW; a client is
// fullscreen when its _NET_WM_STATE contains _NET_WM_STATE_FULLSCREEN.
// Desktop and dock type windows (_NET_WM_WINDOW_TYPE) are skipped, which
// also skips our own wallpaper windows. A client's outputs are the ones its
// root relative rectangle covers at least half of
// (geometry::outputsCoveredBy). The app id is the WM_CLASS class.
//
// Root PropertyNotify, and PropertyNotify / ConfigureNotify on the listed
// clients, schedule a rescan 50 ms later, so a burst of events (a window
// being dragged) costs one rescan. Queries run under an error trap: a
// client may vanish mid scan, which must not abort the process.
#pragma once

#include <X11/Xlib.h>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "display/Platform.h"

namespace canvas {

class EventLoop;
class X11Connection;

class X11Toplevels {
public:
    X11Toplevels(X11Connection& conn, EventLoop& loop);
    ~X11Toplevels();
    X11Toplevels(const X11Toplevels&) = delete;
    X11Toplevels& operator=(const X11Toplevels&) = delete;

    bool start(Platform::ToplevelsCallback cb);

private:
    void onEvent(const XEvent& ev);
    void scheduleRescan();
    void rescan();
    std::vector<Window> windowListProperty(Window w, Atom property);
    std::vector<Atom> atomListProperty(Window w, Atom property);

    X11Connection& conn_;
    EventLoop& loop_;
    Platform::ToplevelsCallback cb_;
    std::set<Window> watched_;  // clients whose events we selected
    uint64_t rescan_timer_ = 0;
    bool started_ = false;
    Atom client_list_ = 0;
    Atom active_window_ = 0;
    Atom wm_state_ = 0;
    Atom wm_state_fullscreen_ = 0;
    Atom wm_window_type_ = 0;
    Atom type_desktop_ = 0;
    Atom type_dock_ = 0;
};

}  // namespace canvas
