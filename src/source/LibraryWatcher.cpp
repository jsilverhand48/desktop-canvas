// Implementation of LibraryWatcher declared in LibraryWatcher.h. See the
// header for the event, debounce and missing-root rules.
#include "source/LibraryWatcher.h"

#include <sys/epoll.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "core/EventLoop.h"
#include "core/Log.h"

namespace canvas {

namespace fs = std::filesystem;

namespace {

constexpr uint32_t kRootMask = IN_CREATE | IN_DELETE | IN_MOVED_FROM |
                               IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF |
                               IN_ONLYDIR;
constexpr uint32_t kItemMask = IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE |
                               IN_MOVED_FROM | IN_CREATE | IN_ONLYDIR;

}  // namespace

LibraryWatcher::LibraryWatcher(EventLoop& loop, std::function<void()> onChange,
                               uint64_t debounceMs, uint64_t maxDelayMs,
                               uint64_t retryMs)
    : loop_(loop),
      onChange_(std::move(onChange)),
      debounceMs_(debounceMs),
      maxDelayMs_(maxDelayMs),
      retryMs_(retryMs) {}

LibraryWatcher::~LibraryWatcher() {
    for (uint64_t* t : {&debounceTimer_, &maxTimer_, &retryTimer_})
        if (*t) loop_.cancelTimer(*t);
    if (fd_ >= 0) {
        loop_.removeFd(fd_);
        close(fd_);
    }
}

bool LibraryWatcher::watch(const std::vector<fs::path>& roots) {
    if (fd_ < 0) {
        fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (fd_ < 0) {
            log::warn() << "library auto refresh unavailable (inotify: "
                        << std::strerror(errno) << ")";
            return false;
        }
        loop_.addFd(fd_, EPOLLIN, [this](uint32_t) { onReadable(); });
    }
    roots_ = roots;

    // Desired set: every existing root plus its item subdirectories.
    std::map<fs::path, bool> wanted;  // path -> is root
    bool missing = false;
    for (const auto& root : roots_) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) {
            missing = true;
            continue;
        }
        wanted[root] = true;
        for (const auto& entry : fs::directory_iterator(root, ec))
            if (entry.is_directory(ec)) wanted.emplace(entry.path(), false);
    }

    std::vector<int> stale;
    for (const auto& [wd, path] : byWd_)
        if (!wanted.count(path)) stale.push_back(wd);
    for (int wd : stale) dropWatch(wd);
    for (const auto& [path, isRoot] : wanted)
        if (!byPath_.count(path)) addWatch(path, isRoot);

    if (missing) armRetry();
    return true;
}

void LibraryWatcher::addWatch(const fs::path& dir, bool isRoot) {
    int wd = inotify_add_watch(fd_, dir.c_str(), isRoot ? kRootMask : kItemMask);
    if (wd < 0) {
        // ENOSPC = fs.inotify.max_user_watches reached; keep going with what
        // is watched rather than failing the whole library.
        log::warn() << "cannot watch " << dir << ": " << std::strerror(errno);
        return;
    }
    // The same inode can come back under a new path (rename); replace it.
    auto old = byWd_.find(wd);
    if (old != byWd_.end()) byPath_.erase(old->second);
    byWd_[wd] = dir;
    byPath_[dir] = wd;
    if (isRoot) rootWds_.insert(wd);
}

void LibraryWatcher::dropWatch(int wd) {
    auto it = byWd_.find(wd);
    if (it == byWd_.end()) return;
    inotify_rm_watch(fd_, wd);  // harmless if the kernel already dropped it
    byPath_.erase(it->second);
    byWd_.erase(it);
    rootWds_.erase(wd);
}

void LibraryWatcher::onReadable() {
    alignas(inotify_event) char buf[16384];
    bool changed = false, rootLost = false;
    while (true) {
        ssize_t n = read(fd_, buf, sizeof(buf));
        if (n <= 0) break;  // EAGAIN: drained
        for (char* p = buf; p < buf + n;) {
            auto* ev = reinterpret_cast<inotify_event*>(p);
            p += sizeof(inotify_event) + ev->len;
            std::string name = ev->len ? ev->name : "";

            if (ev->mask & IN_Q_OVERFLOW) {
                changed = true;  // lost events: rescan everything
                continue;
            }
            auto it = byWd_.find(ev->wd);
            if (it == byWd_.end()) continue;
            bool isRoot = rootWds_.count(ev->wd) > 0;

            if (ev->mask & IN_IGNORED) {
                // The kernel removed the watch: directory deleted, moved
                // away, or its filesystem unmounted.
                byPath_.erase(it->second);
                byWd_.erase(it);
                rootWds_.erase(ev->wd);
                if (isRoot) rootLost = true;
                changed = true;
                continue;
            }
            if (isRoot) {
                if (ev->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) {
                    rootLost = true;
                    changed = true;
                } else if (ev->mask & IN_ISDIR) {
                    fs::path dir = it->second / name;
                    if (ev->mask & (IN_CREATE | IN_MOVED_TO)) {
                        addWatch(dir, false);
                    } else if (auto w = byPath_.find(dir); w != byPath_.end()) {
                        dropWatch(w->second);
                    }
                    changed = true;
                }
            } else if (name == "project.json") {
                // Item completed, replaced or removed.
                changed = true;
            }
        }
    }
    if (rootLost) armRetry();
    if (changed) schedule();
}

void LibraryWatcher::schedule() {
    if (debounceTimer_) loop_.cancelTimer(debounceTimer_);
    debounceTimer_ = loop_.addTimer(debounceMs_, [this] {
        debounceTimer_ = 0;
        fire();
    });
    if (!maxTimer_) {
        maxTimer_ = loop_.addTimer(maxDelayMs_, [this] {
            maxTimer_ = 0;
            fire();
        });
    }
}

void LibraryWatcher::fire() {
    if (debounceTimer_) {
        loop_.cancelTimer(debounceTimer_);
        debounceTimer_ = 0;
    }
    if (maxTimer_) {
        loop_.cancelTimer(maxTimer_);
        maxTimer_ = 0;
    }
    log::info() << "library changed on disk, rescanning";
    onChange_();  // the owner rescans and calls watch() again
}

void LibraryWatcher::armRetry() {
    if (retryTimer_) return;
    retryTimer_ = loop_.addTimer(retryMs_, [this] {
        retryTimer_ = 0;
        auto watchedRoots = [this] {
            size_t n = 0;
            for (const auto& root : roots_) n += byPath_.count(root);
            return n;
        };
        if (rootsProvider_) {
            // Autodetected roots: a library that appeared or vanished changes
            // the list itself; the owner rescans and calls watch() with it.
            if (rootsProvider_() != roots_) schedule();
            armRetry();  // keep polling
            return;
        }
        size_t before = watchedRoots();
        watch(roots_);  // re-arms the retry while any root is still missing
        // A root came back (drive remounted): its items need scanning.
        if (watchedRoots() > before) schedule();
    });
}

void LibraryWatcher::setRootsProvider(
    std::function<std::vector<fs::path>()> provider) {
    rootsProvider_ = std::move(provider);
    if (rootsProvider_) armRetry();
}

}  // namespace canvas
