// Implementation of PackageArchive declared in PackageArchive.h.
#include "scene/assets/PackageArchive.h"

#include <cctype>

#include "scene/assets/ByteReader.h"

namespace canvas::scene {

bool PackageArchive::validName(std::string_view name) {
    if (name.empty() || name.front() == '/') return false;
    if (name.find('\\') != std::string_view::npos) return false;
    if (name.find('\0') != std::string_view::npos) return false;
    size_t start = 0;
    while (start <= name.size()) {
        size_t end = name.find('/', start);
        if (end == std::string_view::npos) end = name.size();
        std::string_view segment = name.substr(start, end - start);
        if (segment.empty() || segment == "." || segment == "..") return false;
        start = end + 1;
    }
    return true;
}

bool PackageArchive::open(const std::filesystem::path& path) {
    if (!file_.open(path)) {
        error_ = file_.error();
        return false;
    }
    return parse(file_.bytes());
}

bool PackageArchive::parse(std::span<const uint8_t> bytes) {
    data_ = bytes;
    entries_.clear();
    index_.clear();
    error_.clear();
    ByteReader in(bytes);
    uint32_t versionLength = in.u32("version length");
    if (in.ok() && versionLength != 8) in.fail("version", "unexpected length");
    version_ = in.string(versionLength, "version");
    if (in.ok()) {
        bool digits = version_.rfind("PKGV", 0) == 0;
        for (size_t i = 4; digits && i < version_.size(); i++)
            digits = std::isdigit(static_cast<unsigned char>(version_[i])) != 0;
        if (!digits) in.fail("version", "not PKGV followed by digits");
    }
    uint32_t count = in.u32("entry count");
    if (in.ok() && count > kMaxEntries) in.fail("entry count", "too many entries");
    struct Raw {
        std::string name;
        uint32_t offset, size;
    };
    std::vector<Raw> raw;
    raw.reserve(in.ok() ? count : 0);
    for (uint32_t i = 0; in.ok() && i < count; i++) {
        uint32_t length = in.u32("name length");
        if (in.ok() && length > kMaxNameLength) in.fail("name", "too long");
        std::string name = in.string(length, "name");
        uint32_t offset = in.u32("entry offset");
        uint32_t size = in.u32("entry size");
        if (in.ok() && !validName(name)) in.fail("name", "unsafe entry name");
        raw.push_back({std::move(name), offset, size});
    }
    if (!in.ok()) {
        error_ = in.error();
        return false;
    }
    const uint64_t base = in.offset();
    for (auto& r : raw) {
        uint64_t begin = base + r.offset;
        if (begin + r.size > bytes.size()) {
            error_ = "entry " + r.name + " runs past the end of the archive";
            return false;
        }
        if (!index_.emplace(r.name, entries_.size()).second) {
            error_ = "duplicate entry " + r.name;
            return false;
        }
        entries_.push_back({std::move(r.name), begin, r.size});
    }
    return true;
}

bool PackageArchive::contains(std::string_view name) const {
    return index_.count(std::string(name)) > 0;
}

std::optional<std::span<const uint8_t>> PackageArchive::read(
    std::string_view name) const {
    auto it = index_.find(std::string(name));
    if (it == index_.end()) return std::nullopt;
    const Entry& e = entries_[it->second];
    return data_.subspan(e.offset, e.size);
}

}  // namespace canvas::scene
