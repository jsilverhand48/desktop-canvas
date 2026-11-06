// Supervises child processes (desktop-canvas-scene and web helper
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
//
// Every child runs in its own process group (signals for stopping go to the
// whole group, so helpers' own subprocesses such as QtWebEngineProcess go
// too), dies with the daemon (PR_SET_PDEATHSIG), and inherits only stdio
// plus, when ChildSpec::controlChannel is set, its end of a fresh control
// socketpair as fd 3 (process/HelperProtocol.h).
//
// Exit handling depends on ChildSpec::exitPolicy. RespawnAlways (the
// web helper, whose exit codes mean nothing to us) treats every exit as a
// failure to respawn. HelperProtocol (our own helpers) ends the child for
// good on exit codes 0 (clean), 2 (usage) and 3 (unsupported; not counted
// as a failure) and treats anything else, including death by signal, as a
// crash. onExit reports the classified exit before any respawn is planned.
#pragma once

#include <sys/types.h>

#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace canvas {

class EventLoop;

enum class ExitPolicy {
    RespawnAlways,   // any exit is a failure; respawn with backoff
    HelperProtocol,  // exit codes per process/HelperProtocol.h
};

// How a child's exit was classified (see ExitPolicy).
enum class ExitKind {
    Clean,        // exit 0 under HelperProtocol: not respawned
    Usage,        // exit 2 under HelperProtocol: not respawned
    Unsupported,  // exit 3 under HelperProtocol: not respawned, no failure
    Crash,        // anything else; always the kind under RespawnAlways
};

struct ChildSpec {
    // argv[0] is the executable, looked up on PATH when it has no slash.
    std::vector<std::string> argv;
    ExitPolicy exitPolicy = ExitPolicy::RespawnAlways;
    // Give every spawn a fresh SOCK_SEQPACKET socketpair: the child gets its
    // end as fd 3, onSpawn receives the parent's end and takes ownership.
    bool controlChannel = false;
    // A descriptor the child gets as fd 4 (helper::kInheritFd), for
    // example the audio spectrum memory; -1 for none. Not owned.
    int inheritFd = -1;
    // After each successful fork (also respawns). controlFd is -1 without a
    // channel; with one the callee must close it (ControlChannel does).
    std::function<void(pid_t pid, int controlFd)> onSpawn;
    // After each exit, before a respawn is scheduled. May call stop() or
    // start() for the same tag.
    std::function<void(ExitKind kind, int waitStatus)> onExit;
};

class ChildSupervisor {
public:
    static constexpr int kMaxFailuresInWindow = 5;
    static constexpr std::chrono::seconds kFailureWindow{120};

    using BrokenCallback = std::function<void(const std::string& tag)>;

    ChildSupervisor(EventLoop& loop);
    ~ChildSupervisor();

    // Starts (and keeps restarting, per the exit policy) a child identified
    // by tag. Calling start again with the same tag terminates the old child
    // (asynchronously) and starts the new one immediately.
    void start(const std::string& tag, ChildSpec spec);
    // Shorthand for a RespawnAlways child without a control channel.
    void start(const std::string& tag, std::vector<std::string> argv);

    // Stops the child and forgets the tag. SIGTERM first, SIGKILL after a
    // grace timer if it lingers; the exit is reaped asynchronously.
    void stop(const std::string& tag);

    void stopAll();
    bool isRunning(const std::string& tag) const;

    // Sends a signal to a running child's main process (SIGSTOP/SIGCONT
    // pause and resume children without a control channel; SIGKILL ends a hung helper,
    // which then counts as a crash). No-op for unknown tags.
    void signalChild(const std::string& tag, int sig);

    // True when pid belongs to a currently supervised child. Used on X11 to
    // recognize scene children's windows for desktop layer demotion.
    bool ownsPid(pid_t pid) const;

    void onBroken(BrokenCallback cb) { broken_cb_ = std::move(cb); }

private:
    struct Child {
        ChildSpec spec;
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
