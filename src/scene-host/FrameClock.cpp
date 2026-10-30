// Implementation of FrameClock declared in FrameClock.h.
#include "scene-host/FrameClock.h"

#include "core/EventLoop.h"

namespace canvas::scenehost {

namespace {
std::chrono::nanoseconds periodFor(int fps) {
    if (fps <= 0) fps = 30;
    return std::chrono::nanoseconds(1000000000LL / fps);
}
}  // namespace

FrameClock::FrameClock(EventLoop& loop, int fps, std::function<void()> tick)
    : loop_(loop), period_(periodFor(fps)), tick_(std::move(tick)) {}

FrameClock::~FrameClock() { stop(); }

void FrameClock::start() {
    if (running_) return;
    running_ = true;
    next_ = std::chrono::steady_clock::now();
    schedule();
}

void FrameClock::stop() {
    running_ = false;
    if (timer_) {
        loop_.cancelTimer(timer_);
        timer_ = 0;
    }
}

void FrameClock::setFps(int fps) { period_ = periodFor(fps); }

void FrameClock::schedule() {
    timer_ = loop_.addTimerAt(next_, [this] {
        timer_ = 0;
        tick_();
        if (!running_) return;  // the tick stopped the clock
        auto now = std::chrono::steady_clock::now();
        next_ += period_;
        // More than a period behind: restart from now, no catch up burst.
        if (next_ + period_ < now) next_ = now;
        schedule();
    });
}

}  // namespace canvas::scenehost
