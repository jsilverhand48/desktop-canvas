// Polls the global X11 pointer position at a fixed rate.
//
// XQueryPointer on the root window works regardless of window stacking or
// occlusion, unlike Wayland where pointer events only reach surfaces under
// the cursor. Root coordinates are mapped to the containing output and
// forwarded as output local positions. Positions are only reported when
// they change, so an idle pointer costs one X round trip per tick and no
// callbacks.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace canvas {

class EventLoop;
class X11Connection;

class X11PointerPoller {
public:
    using Callback =
        std::function<void(const std::string& output, double x, double y)>;

    // rateHz: poll frequency (default 30).
    X11PointerPoller(EventLoop& loop, X11Connection& conn, int rateHz = 30);
    ~X11PointerPoller();

    void setCallback(Callback cb) { callback_ = std::move(cb); }

private:
    void tick();

    EventLoop& loop_;
    X11Connection& conn_;
    int interval_ms_;
    uint64_t timer_ = 0;
    int last_x_ = -1, last_y_ = -1;
    Callback callback_;
};

}  // namespace canvas
