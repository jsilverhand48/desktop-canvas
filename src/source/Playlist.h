// A list of wallpaper ids read from a plain text file.
//
// Format: one workshop id per line. Blank lines are skipped, "#" starts a
// comment (whole line or trailing), and surrounding whitespace is trimmed.
// A missing trailing newline on the last line is fine. Duplicate ids are
// collapsed so a repeated entry does not bias the random draw.
//
// The file is the user's own curated list (typically
// ~/.config/wallpaper-engine/ids.txt); it is read, never written. Ids that
// are not present in the scanned library are kept here and filtered at pick
// time by the caller, so a typo in the file is visible as a skip rather
// than silently dropping the entry on load.
#pragma once

#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace canvas {

class Playlist {
public:
    // Replaces the contents with the ids parsed from path. Returns false if
    // the file cannot be opened; the previous contents are left untouched in
    // that case so a failed reload does not disarm a running rotation.
    bool load(const std::filesystem::path& path);

    void clear();
    bool empty() const { return ids_.empty(); }
    size_t size() const { return ids_.size(); }
    const std::vector<std::string>& ids() const { return ids_; }
    const std::filesystem::path& path() const { return path_; }

    // Random id drawn from the entries accepted by `usable`, avoiding
    // `current` unless it is the only usable entry. Returns "" when nothing
    // is usable. `usable` lets the caller reject ids missing from the
    // library, unsupported types, and the broken blacklist.
    template <typename Predicate>
    std::string pick(const std::string& current, Predicate usable) {
        std::vector<const std::string*> pool;
        for (const auto& id : ids_)
            if (usable(id)) pool.push_back(&id);
        if (pool.empty()) return "";
        if (pool.size() > 1) {
            // Drop the running wallpaper so a rotation tick always visibly
            // changes something.
            std::erase_if(pool, [&](const std::string* p) {
                return *p == current;
            });
        }
        if (pool.empty()) return current;
        std::uniform_int_distribution<size_t> dist(0, pool.size() - 1);
        return *pool[dist(rng_)];
    }

private:
    std::vector<std::string> ids_;
    std::filesystem::path path_;
    std::mt19937 rng_{std::random_device{}()};
};

}  // namespace canvas
