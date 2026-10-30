// Single threaded epoll based event loop.
//
// The daemon multiplexes everything through one loop: the Wayland display fd,
// the X11 connection fd, mpv render wakeups (eventfd), child process exits
// (pidfd), the IPC listening socket and its clients, timers (timerfd), and
// termination signals (signalfd). Callbacks run on the loop thread; no
// internal locking is provided or needed, with one exception: post() may be
// called from any thread (it is how worker threads hand results back).
//
// Timers: addTimer() takes a relative delay; addTimerAt() an absolute
// steady_clock deadline (CLOCK_MONOTONIC on Linux), which frame pacing uses
// so repeated ticks do not accumulate drift.
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <vector>

struct itimerspec;

namespace canvas {

class EventLoop {
public:
    using FdCallback = std::function<void(uint32_t epoll_events)>;
    using TimerCallback = std::function<void()>;

    EventLoop();
    ~EventLoop();
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    // Watches fd for the given epoll event mask (EPOLLIN etc). The callback
    // receives the ready mask. The fd is not owned; call removeFd before
    // closing it.
    void addFd(int fd, uint32_t events, FdCallback cb);
    // Replaces the event mask of an fd previously added with addFd (for
    // example adding EPOLLOUT while a send queue is non empty).
    void modifyFd(int fd, uint32_t events);
    void removeFd(int fd);

    // One shot timer in milliseconds. Returns a timer id usable with
    // cancelTimer. Repeating behavior is built by re-arming in the callback.
    uint64_t addTimer(uint64_t ms, TimerCallback cb);
    // One shot timer at an absolute steady_clock deadline. A deadline in the
    // past fires on the next iteration.
    uint64_t addTimerAt(std::chrono::steady_clock::time_point deadline,
                        TimerCallback cb);
    void cancelTimer(uint64_t id);

    // Runs cb on the loop thread during a later iteration, in posting order.
    // Thread safe; the only EventLoop member that may be called off the loop
    // thread. Callbacks still queued when the loop is destroyed are dropped.
    void post(std::function<void()> cb);

    // Runs until quit() is called. Returns the value passed to quit.
    int run();
    void quit(int code = 0);

    // Installs a handler run when SIGINT or SIGTERM arrives (via signalfd,
    // so it runs on the loop thread like any other callback).
    void onTerminate(std::function<void()> cb);

private:
    struct Timer {
        int fd;
        TimerCallback cb;
    };
    // Creates a timerfd armed with spec (flags: 0 or TFD_TIMER_ABSTIME).
    uint64_t armTimer(const ::itimerspec& spec, int flags, TimerCallback cb);
    void runPosted();

    int epoll_fd_ = -1;
    int signal_fd_ = -1;
    int post_fd_ = -1;  // eventfd woken by post()
    std::mutex post_mutex_;
    std::vector<std::function<void()>> posted_;  // guarded by post_mutex_
    bool running_ = false;
    int exit_code_ = 0;
    uint64_t next_timer_id_ = 1;
    std::map<int, FdCallback> fd_callbacks_;
    std::map<uint64_t, Timer> timers_;
    std::function<void()> terminate_cb_;
};

}  // namespace canvas
