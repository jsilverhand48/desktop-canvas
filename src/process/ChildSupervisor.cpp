// Implementation of ChildSupervisor declared in ChildSupervisor.h.
//
// Children are spawned with fork+execvp; the parent then opens a pidfd via
// pidfd_open and registers it with the event loop. When the pidfd becomes
// readable the child has exited and is reaped with waitpid(WNOHANG); the
// blocking form is never used on the loop thread. Outgoing children
// (replaced or stopped) live in the dying_ table until their exit arrives.
#include "process/ChildSupervisor.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstring>

#include "core/EventLoop.h"
#include "core/Log.h"

namespace canvas {

namespace {
int pidfdOpen(pid_t pid) {
    return static_cast<int>(syscall(SYS_pidfd_open, pid, 0));
}
}  // namespace

ChildSupervisor::ChildSupervisor(EventLoop& loop) : loop_(loop) {}

ChildSupervisor::~ChildSupervisor() { stopAll(); }

void ChildSupervisor::start(const std::string& tag,
                            std::vector<std::string> argv) {
    stop(tag);
    Child child;
    child.argv = std::move(argv);
    children_[tag] = std::move(child);
    spawn(tag);
}

void ChildSupervisor::spawn(const std::string& tag) {
    auto it = children_.find(tag);
    if (it == children_.end()) return;
    Child& child = it->second;

    std::vector<char*> argv;
    for (auto& a : child.argv) argv.push_back(a.data());
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        log::error() << "fork failed for " << tag << ": " << strerror(errno);
        return;
    }
    if (pid == 0) {
        // Child: detach from our signal mask and exec.
        sigset_t mask;
        sigemptyset(&mask);
        sigprocmask(SIG_SETMASK, &mask, nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    child.pid = pid;
    child.pidfd = pidfdOpen(pid);
    log::info() << "spawned " << tag << " pid " << pid << " (" << child.argv[0]
                << ")";
    if (child.pidfd < 0) {
        log::error() << "pidfd_open failed for " << tag;
        return;
    }
    loop_.addFd(child.pidfd, EPOLLIN, [this, tag](uint32_t) {
        handleExit(tag);
    });
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
    if (child.pid > 0) {
        int status = 0;
        waitpid(child.pid, &status, WNOHANG);
        child.pid = -1;
    }

    auto now = std::chrono::steady_clock::now();
    if (now - child.window_start > kFailureWindow) {
        child.window_start = now;
        child.failures = 0;
        child.backoff = std::chrono::milliseconds(1000);
    }
    child.failures++;
    if (child.failures > kMaxFailuresInWindow) {
        log::error() << tag << " failed " << child.failures
                     << " times, marking broken";
        auto cb = broken_cb_;
        children_.erase(it);
        if (cb) cb(tag);
        return;
    }
    log::warn() << tag << " exited, respawning in " << child.backoff.count()
                << "ms (failure " << child.failures << ")";
    child.respawn_timer = loop_.addTimer(child.backoff.count(), [this, tag] {
        auto it2 = children_.find(tag);
        if (it2 == children_.end()) return;
        it2->second.respawn_timer = 0;
        spawn(tag);
    });
    child.backoff =
        std::min(child.backoff * 2, std::chrono::milliseconds(60000));
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
        kill(pid, SIGTERM);
        Dying dying;
        dying.pid = pid;
        dying.kill_timer = loop_.addTimer(3000, [this, pidfd] {
            auto d = dying_.find(pidfd);
            if (d != dying_.end()) {
                log::warn() << "child pid " << d->second.pid
                            << " ignored SIGTERM, sending SIGKILL";
                kill(d->second.pid, SIGKILL);
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
        kill(dying.pid, SIGKILL);
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
