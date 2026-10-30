// Remembers which wallpapers desktop-canvas-scene could not render, so the
// "auto" scene engine does not relaunch the native helper for them on every
// start. See render/InstanceManager.h for how verdicts are used.
//
// A verdict is keyed by wallpaper id and is only valid while neither side
// changed: it stores a stamp (mtime and size) of the wallpaper's main file
// (scene.pkg, else its project "file") and of the helper binary, so new
// content or any rebuilt or updated helper gets a fresh try. Reasons:
// "unsupported" (a strict helper listed features it cannot render) and
// "crash" (the helper kept crashing on this wallpaper).
//
// Persisted as JSON at $XDG_CACHE_HOME/desktop-canvas/native-support.json
// (default ~/.cache/desktop-canvas/...); a missing or corrupt file just
// means everything is retried. Writes replace the file atomically.
#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace canvas {

class NativeSupportCache {
public:
    struct Verdict {
        std::string reason;                 // "unsupported" or "crash"
        std::vector<std::string> features;  // what the helper listed
        std::string contentStamp;
        std::string helperStamp;
    };

    explicit NativeSupportCache(std::filesystem::path file);
    static std::filesystem::path defaultPath();

    // "<mtime ns>:<size>" for a file, empty when it does not exist.
    static std::string stampOf(const std::filesystem::path& file);

    // Reads the file; unreadable or malformed content leaves the cache empty.
    void load();
    // The verdict for id if it was recorded against these stamps.
    const Verdict* find(const std::string& id, const std::string& contentStamp,
                        const std::string& helperStamp) const;
    // Records (replacing any earlier verdict) and saves.
    void record(const std::string& id, Verdict verdict);
    // Drops id's verdict, if any, and saves.
    void forget(const std::string& id);
    const std::map<std::string, Verdict>& all() const { return verdicts_; }

private:
    bool save() const;

    std::filesystem::path file_;
    std::map<std::string, Verdict> verdicts_;
};

}  // namespace canvas
