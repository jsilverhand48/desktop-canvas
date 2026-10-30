// Implementation of the epoll event loop declared in EventLoop.h.
//
// Timers use timerfd so they are just another fd in the epoll set. SIGINT and
// SIGTERM are received through signalfd after being blocked process wide, so
// asynchronous signal handlers are never involved. post() appends to a
// mutex guarded queue and wakes the loop through an eventfd; the loop drains
// the queue on its own thread.
#include "core/EventLoop.h"

#include <signal.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

#include "core/Log.h"

namespace canvas {

EventLoop::EventLoop() {
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) throw std::runtime_error("epoll_create1 failed");

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigprocmask(SIG_BLOCK, &mask, nullptr);
    // SIGCHLD is handled through pidfds in ChildSupervisor, and SIGPIPE from
    // IPC clients must not kill the daemon.
    signal(SIGPIPE, SIG_IGN);
    signal_fd_ = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (signal_fd_ < 0) throw std::runtime_error("signalfd failed");
    addFd(signal_fd_, EPOLLIN, [this](uint32_t) {
        signalfd_siginfo info;
        while (read(signal_fd_, &info, sizeof(info)) == sizeof(info)) {
        }
        log::info() << "termination signal received";
        if (terminate_cb_)
            terminate_cb_();
        else
            quit(0);
    });

    post_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (post_fd_ < 0) throw std::runtime_error("eventfd failed");
    addFd(post_fd_, EPOLLIN, [this](uint32_t) { runPosted(); });
}

EventLoop::~EventLoop() {
    for (auto& [id, timer] : timers_) close(timer.fd);
    if (post_fd_ >= 0) close(post_fd_);
    if (signal_fd_ >= 0) close(signal_fd_);
    if (epoll_fd_ >= 0) close(epoll_fd_);
}

void EventLoop::addFd(int fd, uint32_t events, FdCallback cb) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0)
        throw std::runtime_error("epoll_ctl ADD failed");
    fd_callbacks_[fd] = std::move(cb);
}

void EventLoop::modifyFd(int fd, uint32_t events) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) < 0)
        throw std::runtime_error("epoll_ctl MOD failed");
}

void EventLoop::removeFd(int fd) {
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    fd_callbacks_.erase(fd);
}

uint64_t EventLoop::addTimer(uint64_t ms, TimerCallback cb) {
    itimerspec spec{};
    spec.it_value.tv_sec = static_cast<time_t>(ms / 1000);
    spec.it_value.tv_nsec = static_cast<long>((ms % 1000) * 1000000);
    if (ms == 0) spec.it_value.tv_nsec = 1;  // zero disarms; fire immediately
    return armTimer(spec, 0, std::move(cb));
}

uint64_t EventLoop::addTimerAt(std::chrono::steady_clock::time_point deadline,
                               TimerCallback cb) {
    // steady_clock is CLOCK_MONOTONIC on Linux, the clock the timerfd uses.
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                  deadline.time_since_epoch())
                  .count();
    itimerspec spec{};
    if (ns <= 0) ns = 1;  // zero disarms; the past fires immediately anyway
    spec.it_value.tv_sec = static_cast<time_t>(ns / 1000000000);
    spec.it_value.tv_nsec = static_cast<long>(ns % 1000000000);
    return armTimer(spec, TFD_TIMER_ABSTIME, std::move(cb));
}

uint64_t EventLoop::armTimer(const itimerspec& spec, int flags,
                             TimerCallback cb) {
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (tfd < 0) throw std::runtime_error("timerfd_create failed");
    timerfd_settime(tfd, flags, &spec, nullptr);

    uint64_t id = next_timer_id_++;
    timers_[id] = Timer{tfd, std::move(cb)};
    addFd(tfd, EPOLLIN, [this, id](uint32_t) {
        auto it = timers_.find(id);
        if (it == timers_.end()) return;
        uint64_t expirations;
        (void)read(it->second.fd, &expirations, sizeof(expirations));
        TimerCallback cb = it->second.cb;  // copy: cb may cancel the timer
        cancelTimer(id);
        cb();
    });
    return id;
}

void EventLoop::post(std::function<void()> cb) {
    {
        std::lock_guard<std::mutex> lock(post_mutex_);
        posted_.push_back(std::move(cb));
    }
    uint64_t one = 1;
    (void)write(post_fd_, &one, sizeof(one));
}

void EventLoop::runPosted() {
    uint64_t count;
    (void)read(post_fd_, &count, sizeof(count));
    std::vector<std::function<void()>> batch;
    {
        std::lock_guard<std::mutex> lock(post_mutex_);
        batch.swap(posted_);
    }
    // Callbacks posted while this batch runs land in posted_ and wake the
    // eventfd again, so they run on a later iteration, never recursively.
    for (auto& cb : batch) cb();
}

void EventLoop::cancelTimer(uint64_t id) {
    auto it = timers_.find(id);
    if (it == timers_.end()) return;
    removeFd(it->second.fd);
    close(it->second.fd);
    timers_.erase(it);
}

int EventLoop::run() {
    running_ = true;
    std::vector<epoll_event> events(32);
    while (running_) {
        int n = epoll_wait(epoll_fd_, events.data(),
                           static_cast<int>(events.size()), -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            log::error() << "epoll_wait: " << strerror(errno);
            return 1;
        }
        for (int i = 0; i < n && running_; i++) {
            auto it = fd_callbacks_.find(events[i].data.fd);
            if (it == fd_callbacks_.end()) continue;
            // Copy so a callback that calls removeFd on its own fd does not
            // destroy itself (and its captures) mid execution.
            FdCallback cb = it->second;
            cb(events[i].events);
        }
    }
    return exit_code_;
}

void EventLoop::quit(int code) {
    running_ = false;
    exit_code_ = code;
}

void EventLoop::onTerminate(std::function<void()> cb) {
    terminate_cb_ = std::move(cb);
}

}  // namespace canvas
