// Read only memory map of a whole file (RAII).
//
// Packages can be hundreds of megabytes (333 MB is the largest in the
// corpus); mapping them lets entries be served as views without copying and
// lets the kernel page in only what is read. An empty file maps to an empty
// span. Moving transfers the mapping.
//
// openBeneath() opens a path relative to a root directory and refuses any
// resolution that leaves the root (absolute paths, "..", symlinks pointing
// outside), using openat2(RESOLVE_BENEATH) where the kernel has it (Linux
// 5.6+) and a canonical path check otherwise. Lookups into workshop items
// and the assets folder go through it (src/scene/assets/README.md
// section 2).
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace canvas::scene {

class MappedFile {
public:
    MappedFile() = default;
    ~MappedFile();
    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    // Maps path; false (with error()) when it cannot be opened or mapped or
    // is not a regular file.
    bool open(const std::filesystem::path& path);
    // Maps root/relative without leaving root; false (with error()) as above
    // or when the path resolves outside root.
    bool openBeneath(const std::filesystem::path& root, std::string_view relative);
    void close();

    bool isOpen() const { return open_; }
    std::span<const uint8_t> bytes() const {
        return {static_cast<const uint8_t*>(data_), size_};
    }
    const std::string& error() const { return error_; }

private:
    // Maps the regular file behind fd and closes fd.
    bool mapFd(int fd, const std::string& name);

    void* data_ = nullptr;
    size_t size_ = 0;
    bool open_ = false;
    std::string error_;
};

}  // namespace canvas::scene
