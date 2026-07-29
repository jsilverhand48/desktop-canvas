// Single threaded epoll based event loop.
//
// The daemon multiplexes everything through one loop: the Wayland display fd,
// the X11 connection fd, mpv render wakeups (eventfd), child process exits
// (pidfd), the IPC listening socket and its clients, timers (timerfd), and
// termination signals (signalfd). Callbacks run on the loop thread; no
// internal locking is provided or needed.
#pragma once

#include <cstdint>
#include <functional>
#include <map>

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
    void removeFd(int fd);

    // One shot timer in milliseconds. Returns a timer id usable with
    // cancelTimer. Repeating behavior is built by re-arming in the callback.
    uint64_t addTimer(uint64_t ms, TimerCallback cb);
    void cancelTimer(uint64_t id);

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
    int epoll_fd_ = -1;
    int signal_fd_ = -1;
    bool running_ = false;
    int exit_code_ = 0;
    uint64_t next_timer_id_ = 1;
    std::map<int, FdCallback> fd_callbacks_;
    std::map<uint64_t, Timer> timers_;
    std::function<void()> terminate_cb_;
};

}  // namespace canvas
