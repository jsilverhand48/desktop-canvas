// Supervises child processes (linux-wallpaperengine and web helper
// instances).
//
// Each spawned child is watched through a pidfd registered with the event
// loop, so exits are observed without SIGCHLD handlers. A child that dies
// while it is still wanted is respawned with exponential backoff
// (1s, 2s, 4s, ... capped at 60s). After kMaxFailuresInWindow rapid failures
// the supervisor gives up and reports the child broken through the onBroken
// callback, letting the daemon blacklist the wallpaper.
//
// Replacing or stopping a child (start() on an existing tag, stop()) moves
// the outgoing process into a separate "dying" table keyed by its pidfd,
// with its own exit handler and SIGKILL escalation timer. This keeps the
// per tag entry free for the replacement immediately and guarantees the
// old child's exit can never be confused with the new one (a tag keyed
// lookup once caused a blocking waitpid on the wrong, still running pid).
#pragma once

#include <sys/types.h>

#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace canvas {

class EventLoop;

class ChildSupervisor {
public:
    static constexpr int kMaxFailuresInWindow = 5;
    static constexpr std::chrono::seconds kFailureWindow{120};

    using BrokenCallback = std::function<void(const std::string& tag)>;

    ChildSupervisor(EventLoop& loop);
    ~ChildSupervisor();

    // Starts (and keeps restarting) a child identified by tag. argv[0] is the
    // executable. Calling start again with the same tag terminates the old
    // child (asynchronously) and starts the new command immediately.
    void start(const std::string& tag, std::vector<std::string> argv);

    // Stops the child and forgets the tag. SIGTERM first, SIGKILL after a
    // grace timer if it lingers; the exit is reaped asynchronously.
    void stop(const std::string& tag);

    void stopAll();
    bool isRunning(const std::string& tag) const;

    // Sends a signal to a running child (SIGSTOP/SIGCONT implement
    // pause/resume for scene wallpapers). No-op for unknown tags.
    void signalChild(const std::string& tag, int sig);

    // True when pid belongs to a currently supervised child. Used on X11 to
    // recognize scene children's windows for desktop layer demotion.
    bool ownsPid(pid_t pid) const;

    void onBroken(BrokenCallback cb) { broken_cb_ = std::move(cb); }

private:
    struct Child {
        std::vector<std::string> argv;
        pid_t pid = -1;
        int pidfd = -1;
        int failures = 0;
        std::chrono::steady_clock::time_point window_start{};
        std::chrono::milliseconds backoff{1000};
        uint64_t respawn_timer = 0;
    };
    struct Dying {
        pid_t pid = -1;
        uint64_t kill_timer = 0;
    };

    void spawn(const std::string& tag);
    // Takes tag by value: the caller is the pidfd callback whose capture
    // owns the string, and handleExit removes that callback.
    void handleExit(std::string tag);
    void handleDyingExit(int pidfd);

    EventLoop& loop_;
    std::map<std::string, Child> children_;
    std::map<int, Dying> dying_;  // key: pidfd
    BrokenCallback broken_cb_;
};

}  // namespace canvas
