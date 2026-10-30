// Implementation of X11Toplevels declared in X11Toplevels.h.
#include "display/x11/X11Toplevels.h"

#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <algorithm>

#include "core/EventLoop.h"
#include "core/Log.h"
#include "display/OutputGeometry.h"
#include "display/x11/X11Connection.h"

namespace canvas {

namespace {

// Counts errors instead of letting Xlib's default handler exit the
// process. Only installed around rescans; XSync before restoring makes
// sure every error of the scan has arrived.
int g_trappedErrors = 0;
int trapError(Display*, XErrorEvent*) {
    g_trappedErrors++;
    return 0;
}

class ErrorTrap {
public:
    explicit ErrorTrap(Display* dpy) : dpy_(dpy) {
        g_trappedErrors = 0;
        previous_ = XSetErrorHandler(trapError);
    }
    ~ErrorTrap() {
        XSync(dpy_, False);
        XSetErrorHandler(previous_);
    }

private:
    Display* dpy_;
    int (*previous_)(Display*, XErrorEvent*);
};

}  // namespace

X11Toplevels::X11Toplevels(X11Connection& conn, EventLoop& loop)
    : conn_(conn), loop_(loop) {}

X11Toplevels::~X11Toplevels() {
    if (rescan_timer_) loop_.cancelTimer(rescan_timer_);
}

bool X11Toplevels::start(Platform::ToplevelsCallback cb) {
    cb_ = std::move(cb);
    if (started_) return true;
    started_ = true;
    client_list_ = conn_.atom("_NET_CLIENT_LIST");
    active_window_ = conn_.atom("_NET_ACTIVE_WINDOW");
    wm_state_ = conn_.atom("_NET_WM_STATE");
    wm_state_fullscreen_ = conn_.atom("_NET_WM_STATE_FULLSCREEN");
    wm_window_type_ = conn_.atom("_NET_WM_WINDOW_TYPE");
    type_desktop_ = conn_.atom("_NET_WM_WINDOW_TYPE_DESKTOP");
    type_dock_ = conn_.atom("_NET_WM_WINDOW_TYPE_DOCK");
    conn_.addRootEventMask(PropertyChangeMask);
    conn_.addEventObserver([this](const XEvent& ev) { onEvent(ev); });
    log::info() << "watching toplevel windows (EWMH)";
    rescan();
    return true;
}

void X11Toplevels::onEvent(const XEvent& ev) {
    if (ev.type == PropertyNotify) {
        Window w = ev.xproperty.window;
        Atom a = ev.xproperty.atom;
        if (w == conn_.root() ? (a == client_list_ || a == active_window_)
                              : (watched_.count(w) && a == wm_state_))
            scheduleRescan();
    } else if (ev.type == ConfigureNotify && watched_.count(ev.xconfigure.window)) {
        scheduleRescan();
    } else if (ev.type == DestroyNotify) {
        watched_.erase(ev.xdestroywindow.window);
    }
}

void X11Toplevels::scheduleRescan() {
    if (rescan_timer_) return;
    rescan_timer_ = loop_.addTimer(50, [this] {
        rescan_timer_ = 0;
        rescan();
    });
}

std::vector<Window> X11Toplevels::windowListProperty(Window w, Atom property) {
    std::vector<Window> result;
    Atom type;
    int format;
    unsigned long count = 0, after = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(conn_.display(), w, property, 0, 4096, False,
                           XA_WINDOW, &type, &format, &count, &after,
                           &data) == Success &&
        data && format == 32) {
        auto* windows = reinterpret_cast<Window*>(data);
        result.assign(windows, windows + count);
    }
    if (data) XFree(data);
    return result;
}

std::vector<Atom> X11Toplevels::atomListProperty(Window w, Atom property) {
    std::vector<Atom> result;
    Atom type;
    int format;
    unsigned long count = 0, after = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(conn_.display(), w, property, 0, 1024, False,
                           XA_ATOM, &type, &format, &count, &after,
                           &data) == Success &&
        data && format == 32) {
        auto* atoms = reinterpret_cast<Atom*>(data);
        result.assign(atoms, atoms + count);
    }
    if (data) XFree(data);
    return result;
}

void X11Toplevels::rescan() {
    Display* dpy = conn_.display();
    std::vector<OutputDesc> outputs;
    for (const auto& o : conn_.outputs())
        outputs.push_back(OutputDesc{o.name, o.width, o.height, o.x, o.y});

    std::vector<ToplevelInfo> windows;
    {
        ErrorTrap trap(dpy);
        auto active = windowListProperty(conn_.root(), active_window_);
        Window activeWindow = active.empty() ? 0 : active.front();
        auto clients = windowListProperty(conn_.root(), client_list_);
        for (Window w : clients) {
            if (!watched_.count(w)) {
                XSelectInput(dpy, w, PropertyChangeMask | StructureNotifyMask);
                watched_.insert(w);
            }
            auto types = atomListProperty(w, wm_window_type_);
            if (std::find(types.begin(), types.end(), type_desktop_) != types.end() ||
                std::find(types.begin(), types.end(), type_dock_) != types.end())
                continue;
            ToplevelInfo info;
            auto state = atomListProperty(w, wm_state_);
            info.fullscreen = std::find(state.begin(), state.end(),
                                        wm_state_fullscreen_) != state.end();
            info.active = w == activeWindow;
            XClassHint hint{};
            if (XGetClassHint(dpy, w, &hint)) {
                if (hint.res_class) info.appId = hint.res_class;
                if (hint.res_name) XFree(hint.res_name);
                if (hint.res_class) XFree(hint.res_class);
            }
            XWindowAttributes attrs{};
            int rx = 0, ry = 0;
            Window child;
            if (XGetWindowAttributes(dpy, w, &attrs) &&
                XTranslateCoordinates(dpy, w, conn_.root(), 0, 0, &rx, &ry,
                                      &child))
                info.outputs = geometry::outputsCoveredBy(
                    outputs, rx, ry, attrs.width, attrs.height);
            windows.push_back(std::move(info));
        }
        // Forget clients that are no longer listed.
        std::set<Window> listed(clients.begin(), clients.end());
        std::erase_if(watched_, [&](Window w) { return !listed.count(w); });
    }
    if (cb_) cb_(windows);
}

}  // namespace canvas
