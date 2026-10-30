// Watches the wallpaper library for added and removed items (inotify).
//
// Each content root (a steamapps/workshop/content/431960 style directory)
// is watched for subdirectories appearing, disappearing or being renamed,
// and every item directory is watched for its project.json being written,
// moved in or deleted. Steam creates an item's directory before the
// download finishes, so the project.json event is what makes a new
// wallpaper show up once it is complete.
//
// Events are debounced: onChange runs once `debounceMs` after the last
// relevant event, but no later than `maxDelayMs` after the first, so a long
// download that keeps writing still produces updates.
//
// Roots that do not exist (yet), or that disappear (the drive holding the
// Steam library is unmounted, or the directory is removed), are re-checked
// every `retryMs`; when one comes back it is watched again and onChange
// fires, since its whole contents just appeared. Losing a root fires
// onChange too, since its items are gone.
//
// watch() may be called repeatedly (after every rescan); it adds and drops
// inotify watches by difference, so unchanged directories keep theirs.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <vector>

namespace canvas {

class EventLoop;

class LibraryWatcher {
public:
    LibraryWatcher(EventLoop& loop, std::function<void()> onChange,
                   uint64_t debounceMs = 1500, uint64_t maxDelayMs = 10000,
                   uint64_t retryMs = 30000);
    ~LibraryWatcher();

    LibraryWatcher(const LibraryWatcher&) = delete;
    LibraryWatcher& operator=(const LibraryWatcher&) = delete;

    // Sets the roots to watch (and their item directories). False when
    // inotify is unavailable; the daemon then simply has no auto refresh.
    bool watch(const std::vector<std::filesystem::path>& roots);

    // For autodetected roots: every retryMs the provider is asked for the
    // current root list (e.g. WorkshopScanner::defaultRoots(), which only
    // returns existing directories), and onChange fires when it differs, so
    // a Steam library drive mounted after startup is picked up.
    void setRootsProvider(std::function<std::vector<std::filesystem::path>()> p);

    // Number of directories currently watched (roots + items); for tests.
    size_t watchedCount() const { return byWd_.size(); }

private:
    void onReadable();
    void addWatch(const std::filesystem::path& dir, bool isRoot);
    void dropWatch(int wd);
    void schedule();
    void fire();
    void armRetry();

    EventLoop& loop_;
    std::function<void()> onChange_;
    std::function<std::vector<std::filesystem::path>()> rootsProvider_;
    uint64_t debounceMs_, maxDelayMs_, retryMs_;
    int fd_ = -1;
    std::vector<std::filesystem::path> roots_;
    std::map<int, std::filesystem::path> byWd_;
    std::map<std::filesystem::path, int> byPath_;
    std::set<int> rootWds_;
    uint64_t debounceTimer_ = 0;
    uint64_t maxTimer_ = 0;
    uint64_t retryTimer_ = 0;
};

}  // namespace canvas
