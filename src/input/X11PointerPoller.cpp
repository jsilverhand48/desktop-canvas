// Implementation of X11PointerPoller declared in X11PointerPoller.h.
#include "input/X11PointerPoller.h"

#include <X11/Xlib.h>

#include "core/EventLoop.h"
#include "display/x11/X11Connection.h"

namespace canvas {

X11PointerPoller::X11PointerPoller(EventLoop& loop, X11Connection& conn,
                                   int rateHz)
    : loop_(loop),
      conn_(conn),
      interval_ms_(rateHz > 0 ? 1000 / rateHz : 33) {
    timer_ = loop_.addTimer(interval_ms_, [this] { tick(); });
}

X11PointerPoller::~X11PointerPoller() {
    if (timer_) loop_.cancelTimer(timer_);
}

void X11PointerPoller::tick() {
    // Re-arm first so an exception in the callback cannot stop the poller.
    timer_ = loop_.addTimer(interval_ms_, [this] { tick(); });

    Window root_ret, child_ret;
    int root_x = 0, root_y = 0, win_x = 0, win_y = 0;
    unsigned int mask = 0;
    if (!XQueryPointer(conn_.display(), conn_.root(), &root_ret, &child_ret,
                       &root_x, &root_y, &win_x, &win_y, &mask))
        return;
    if (root_x == last_x_ && root_y == last_y_) return;
    last_x_ = root_x;
    last_y_ = root_y;
    if (!callback_) return;
    for (const auto& out : conn_.outputs()) {
        if (root_x >= out.x && root_x < out.x + out.width && root_y >= out.y &&
            root_y < out.y + out.height) {
            callback_(out.name, root_x - out.x, root_y - out.y);
            return;
        }
    }
}

}  // namespace canvas
