// Fixed rate ticker on the EventLoop.
//
// Ticks are scheduled on absolute steady_clock deadlines
// (EventLoop::addTimerAt), so a slow frame delays only itself and the long
// run rate stays exact. A tick that runs late by more than a period (a long
// load, a suspended machine) restarts the schedule from now: the clock
// never fires a burst of catch up ticks. The first tick after start() is
// immediate. The tick callback may call stop().
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>

namespace canvas {
class EventLoop;
}

namespace canvas::scenehost {

class FrameClock {
public:
    FrameClock(EventLoop& loop, int fps, std::function<void()> tick);
    ~FrameClock();
    FrameClock(const FrameClock&) = delete;
    FrameClock& operator=(const FrameClock&) = delete;

    void start();
    void stop();
    bool running() const { return running_; }
    // Takes effect from the next scheduled tick. Non positive means 30.
    void setFps(int fps);
    std::chrono::nanoseconds period() const { return period_; }

private:
    void schedule();

    EventLoop& loop_;
    std::chrono::nanoseconds period_;
    std::function<void()> tick_;
    std::chrono::steady_clock::time_point next_{};
    uint64_t timer_ = 0;
    bool running_ = false;
};

}  // namespace canvas::scenehost
