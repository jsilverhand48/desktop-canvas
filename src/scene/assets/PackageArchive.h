// scene.pkg archives, per src/scene/assets/README.md section 1.
//
// open() maps the file read only and validates the whole table up front:
// version tag, entry count and name length limits, safe names (relative,
// no backslash, no "." or ".." segments, unique), and every entry inside
// the file. After that, read() hands out views into the mapping without
// copying; they stay valid while the archive lives. parse() validates an
// archive held in memory by the caller (tests), with the same rules.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "scene/assets/MappedFile.h"

namespace canvas::scene {

class PackageArchive {
public:
    static constexpr uint32_t kMaxEntries = 65536;
    static constexpr uint32_t kMaxNameLength = 4096;

    struct Entry {
        std::string name;
        uint64_t offset = 0;  // from the start of the file
        uint64_t size = 0;
    };

    bool open(const std::filesystem::path& path);
    bool parse(std::span<const uint8_t> bytes);

    const std::string& version() const { return version_; }
    const std::vector<Entry>& entries() const { return entries_; }
    bool contains(std::string_view name) const;
    std::optional<std::span<const uint8_t>> read(std::string_view name) const;
    const std::string& error() const { return error_; }

    // The name rule above, shared with AssetLocator's normalization.
    static bool validName(std::string_view name);

private:
    MappedFile file_;
    std::span<const uint8_t> data_;
    std::string version_;
    std::string error_;
    std::vector<Entry> entries_;
    std::unordered_map<std::string, size_t> index_;
};

}  // namespace canvas::scene
