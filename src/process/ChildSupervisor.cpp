// Implementation of ChildSupervisor declared in ChildSupervisor.h.
//
// Children are spawned with fork+execvp; the parent then opens a pidfd via
// pidfd_open and registers it with the event loop. When the pidfd becomes
// readable the child has exited and is reaped with waitpid(WNOHANG); the
// blocking form is never used on the loop thread. Outgoing children
// (replaced or stopped) live in the dying_ table until their exit arrives.
//
// Between fork and exec the child only makes async signal safe calls (the
// daemon has libmpv threads): signal mask reset, setpgid, prctl, dup2,
// fcntl and close_range. argv is built before forking.
#include "process/ChildSupervisor.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <climits>
#include <cstring>

#include "core/ControlChannel.h"
#include "core/EventLoop.h"
#include "core/Log.h"
#include "process/HelperProtocol.h"

namespace canvas {

namespace {
int pidfdOpen(pid_t pid) {
    return static_cast<int>(syscall(SYS_pidfd_open, pid, 0));
}

// Every child leads its own process group (pgid == pid); signal the group,
// falling back to the process if the group is already gone.
void killGroup(pid_t pid, int sig) {
    if (kill(-pid, sig) < 0) kill(pid, sig);
}

ExitKind classify(ExitPolicy policy, int status) {
    if (policy != ExitPolicy::HelperProtocol || !WIFEXITED(status))
        return ExitKind::Crash;
    switch (WEXITSTATUS(status)) {
        case helper::kExitClean: return ExitKind::Clean;
        case helper::kExitUsage: return ExitKind::Usage;
        case helper::kExitUnsupported: return ExitKind::Unsupported;
        default: return ExitKind::Crash;
    }
}

std::string describe(int status) {
    if (WIFEXITED(status))
        return "exit code " + std::to_string(WEXITSTATUS(status));
    if (WIFSIGNALED(status))
        return std::string("signal ") + strsignal(WTERMSIG(status));
    return "unknown status";
}
}  // namespace

ChildSupervisor::ChildSupervisor(EventLoop& loop) : loop_(loop) {}

ChildSupervisor::~ChildSupervisor() { stopAll(); }

void ChildSupervisor::start(const std::string& tag, ChildSpec spec) {
    stop(tag);
    Child child;
    child.spec = std::move(spec);
    children_[tag] = std::move(child);
    spawn(tag);
}

void ChildSupervisor::start(const std::string& tag,
                            std::vector<std::string> argv) {
    ChildSpec spec;
    spec.argv = std::move(argv);
    start(tag, std::move(spec));
}

void ChildSupervisor::spawn(const std::string& tag) {
    auto it = children_.find(tag);
    if (it == children_.end()) return;
    Child& child = it->second;

    std::vector<char*> argv;
    for (auto& a : child.spec.argv) argv.push_back(a.data());
    argv.push_back(nullptr);

    // fds[0] stays with us, fds[1] becomes the child's fd 3. Both are
    // CLOEXEC; the child clears the flag on its copy only.
    int fds[2] = {-1, -1};
    if (child.spec.controlChannel && !ControlChannel::makePair(fds)) {
        log::error() << "no control channel for " << tag << ", not starting";
        return;
    }

    const int inherit = child.spec.inheritFd;
    pid_t parent = getpid();
    pid_t pid = fork();
    if (pid < 0) {
        log::error() << "fork failed for " << tag << ": " << strerror(errno);
        if (fds[0] >= 0) close(fds[0]);
        if (fds[1] >= 0) close(fds[1]);
        return;
    }
    if (pid == 0) {
        // Child: detach from our signal mask, lead a process group, die with
        // the daemon, keep only stdio (+ the control socket) and exec.
        sigset_t mask;
        sigemptyset(&mask);
        sigprocmask(SIG_SETMASK, &mask, nullptr);
        setpgid(0, 0);
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        if (getppid() != parent) _exit(127);  // daemon already gone
        int keep = 2;
        // Out of the way of fd 3 and 4 first, so neither dup2 clobbers it.
        const int extra = inherit >= 0 ? fcntl(inherit, F_DUPFD, 10) : -1;
        if (inherit >= 0 && extra < 0) _exit(127);
        if (fds[1] >= 0) {
            if (fds[1] != helper::kControlFd &&
                dup2(fds[1], helper::kControlFd) < 0)
                _exit(127);
            fcntl(helper::kControlFd, F_SETFD, 0);
            keep = helper::kControlFd;
        }
        if (extra >= 0) {
            if (dup2(extra, helper::kInheritFd) < 0) _exit(127);
            fcntl(helper::kInheritFd, F_SETFD, 0);
            keep = helper::kInheritFd;
        }
        close_range(static_cast<unsigned>(keep + 1), UINT_MAX, 0);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    // Also set the group from this side so it exists before we might signal
    // it, whichever process runs first.
    setpgid(pid, pid);
    if (fds[1] >= 0) close(fds[1]);
    child.pid = pid;
    child.pidfd = pidfdOpen(pid);
    log::info() << "spawned " << tag << " pid " << pid << " ("
                << child.spec.argv[0] << ")";
    if (child.pidfd < 0) {
        log::error() << "pidfd_open failed for " << tag;
        if (fds[0] >= 0) close(fds[0]);
        return;
    }
    loop_.addFd(child.pidfd, EPOLLIN, [this, tag](uint32_t) {
        handleExit(tag);
    });
    if (child.spec.onSpawn) {
        auto cb = child.spec.onSpawn;  // copy: the callback may restart tag
        cb(pid, fds[0]);
    } else if (fds[0] >= 0) {
        close(fds[0]);
    }
}

void ChildSupervisor::handleExit(std::string tag) {
    auto it = children_.find(tag);
    if (it == children_.end()) return;
    Child& child = it->second;
    if (child.pidfd >= 0) {
        loop_.removeFd(child.pidfd);
        close(child.pidfd);
        child.pidfd = -1;
    }
    int status = 0;
    if (child.pid > 0) {
        if (waitpid(child.pid, &status, WNOHANG) <= 0)
            status = W_EXITCODE(127, 0);  // unknown: treat as a crash
        child.pid = -1;
    }

    ExitKind kind = classify(child.spec.exitPolicy, status);
    auto onExit = child.spec.onExit;  // copy: may stop or restart this tag
    if (kind != ExitKind::Crash) {
        if (kind == ExitKind::Usage)
            log::error() << tag << " rejected its command line ("
                         << describe(status) << "), not respawning";
        else
            log::info() << tag << " finished ("
                        << (kind == ExitKind::Clean ? "clean exit"
                                                    : "unsupported content")
                        << "), not respawning";
        children_.erase(it);
        if (onExit) onExit(kind, status);
        return;
    }
    if (onExit) {
        onExit(kind, status);
        it = children_.find(tag);
        if (it == children_.end()) return;  // the callback stopped it
        // The callback started a replacement under this tag.
        if (it->second.pid > 0 || it->second.respawn_timer) return;
    }
    Child& crashed = it->second;

    auto now = std::chrono::steady_clock::now();
    if (now - crashed.window_start > kFailureWindow) {
        crashed.window_start = now;
        crashed.failures = 0;
        crashed.backoff = std::chrono::milliseconds(1000);
    }
    crashed.failures++;
    if (crashed.failures > kMaxFailuresInWindow) {
        log::error() << tag << " failed " << crashed.failures
                     << " times, marking broken";
        auto cb = broken_cb_;
        children_.erase(it);
        if (cb) cb(tag);
        return;
    }
    log::warn() << tag << " exited (" << describe(status) << "), respawning in "
                << crashed.backoff.count() << "ms (failure " << crashed.failures
                << ")";
    crashed.respawn_timer = loop_.addTimer(crashed.backoff.count(), [this, tag] {
        auto it2 = children_.find(tag);
        if (it2 == children_.end()) return;
        it2->second.respawn_timer = 0;
        spawn(tag);
    });
    crashed.backoff =
        std::min(crashed.backoff * 2, std::chrono::milliseconds(60000));
}

void ChildSupervisor::stop(const std::string& tag) {
    auto it = children_.find(tag);
    if (it == children_.end()) return;
    Child& child = it->second;
    if (child.respawn_timer) loop_.cancelTimer(child.respawn_timer);

    if (child.pid > 0 && child.pidfd >= 0) {
        // Hand the process over to the dying table; its exit handler and
        // SIGKILL escalation are keyed by pidfd so they can never touch a
        // replacement child started under the same tag.
        int pidfd = child.pidfd;
        pid_t pid = child.pid;
        loop_.removeFd(pidfd);
        killGroup(pid, SIGTERM);
        Dying dying;
        dying.pid = pid;
        dying.kill_timer = loop_.addTimer(3000, [this, pidfd] {
            auto d = dying_.find(pidfd);
            if (d != dying_.end()) {
                log::warn() << "child pid " << d->second.pid
                            << " ignored SIGTERM, sending SIGKILL";
                killGroup(d->second.pid, SIGKILL);
            }
        });
        dying_[pidfd] = dying;
        loop_.addFd(pidfd, EPOLLIN,
                    [this, pidfd](uint32_t) { handleDyingExit(pidfd); });
    }
    children_.erase(it);
}

void ChildSupervisor::handleDyingExit(int pidfd) {
    auto it = dying_.find(pidfd);
    if (it == dying_.end()) return;
    if (it->second.kill_timer) loop_.cancelTimer(it->second.kill_timer);
    loop_.removeFd(pidfd);
    close(pidfd);
    waitpid(it->second.pid, nullptr, WNOHANG);
    dying_.erase(it);
}

void ChildSupervisor::stopAll() {
    std::vector<std::string> tags;
    for (auto& [tag, child] : children_) tags.push_back(tag);
    for (auto& tag : tags) stop(tag);
    // Shutdown path: no event loop iterations remain, so finish the dying
    // children synchronously. SIGKILL guarantees a prompt exit.
    for (auto& [pidfd, dying] : dying_) {
        if (dying.kill_timer) loop_.cancelTimer(dying.kill_timer);
        loop_.removeFd(pidfd);
        close(pidfd);
        killGroup(dying.pid, SIGKILL);
        waitpid(dying.pid, nullptr, 0);
    }
    dying_.clear();
}

bool ChildSupervisor::isRunning(const std::string& tag) const {
    auto it = children_.find(tag);
    return it != children_.end() && it->second.pid > 0;
}

void ChildSupervisor::signalChild(const std::string& tag, int sig) {
    auto it = children_.find(tag);
    if (it != children_.end() && it->second.pid > 0)
        kill(it->second.pid, sig);
}

bool ChildSupervisor::ownsPid(pid_t pid) const {
    for (const auto& [tag, child] : children_)
        if (child.pid == pid) return true;
    return false;
}

}  // namespace canvas
