// Implementation of MappedFile declared in MappedFile.h.
#include "scene/assets/MappedFile.h"

#include <fcntl.h>
#include <linux/openat2.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <utility>

namespace canvas::scene {

namespace {

// openat2 has no glibc wrapper everywhere; call it directly. -1 with errno
// ENOSYS on kernels before 5.6.
int openBeneathFd(int dirfd, const std::string& relative) {
#ifdef SYS_openat2
    struct open_how how {};
    how.flags = O_RDONLY | O_CLOEXEC | O_NOCTTY;
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_MAGICLINKS;
    return static_cast<int>(
        syscall(SYS_openat2, dirfd, relative.c_str(), &how, sizeof(how)));
#else
    (void)dirfd;
    (void)relative;
    errno = ENOSYS;
    return -1;
#endif
}

// Fallback for kernels without openat2: the canonical target must stay
// below the canonical root. Returns 0, or the errno to report (ENOENT when
// the target is missing, EXDEV as RESOLVE_BENEATH does when it escapes).
// Racy against concurrent symlink swaps, which only matters for content
// that changes while it is read.
int checkBeneath(const std::filesystem::path& root, const std::filesystem::path& full) {
    std::error_code ec;
    auto base = std::filesystem::canonical(root, ec);
    if (ec) return ENOENT;
    auto target = std::filesystem::canonical(full, ec);
    if (ec) return ENOENT;
    auto b = base.begin();
    auto t = target.begin();
    for (; b != base.end(); ++b, ++t)
        if (t == target.end() || *b != *t) return EXDEV;
    return 0;
}

}  // namespace

MappedFile::~MappedFile() { close(); }

MappedFile::MappedFile(MappedFile&& other) noexcept { *this = std::move(other); }

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this != &other) {
        close();
        data_ = std::exchange(other.data_, nullptr);
        size_ = std::exchange(other.size_, 0);
        open_ = std::exchange(other.open_, false);
        error_ = std::move(other.error_);
    }
    return *this;
}

bool MappedFile::open(const std::filesystem::path& path) {
    close();
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOCTTY);
    if (fd < 0) {
        error_ = "cannot open " + path.string() + ": " + strerror(errno);
        return false;
    }
    return mapFd(fd, path.string());
}

bool MappedFile::openBeneath(const std::filesystem::path& root,
                             std::string_view relative) {
    close();
    std::string rel(relative);
    std::string name = (root / rel).string();
    if (rel.empty() || rel.front() == '/') {
        error_ = name + " is not a relative path";
        return false;
    }
    int dirfd = ::open(root.c_str(), O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (dirfd < 0) {
        error_ = "cannot open " + root.string() + ": " + strerror(errno);
        return false;
    }
    int fd = openBeneathFd(dirfd, rel);
    if (fd < 0 && errno == ENOSYS) {
        if (int why = checkBeneath(root, root / rel))
            errno = why;
        else
            fd = ::openat(dirfd, rel.c_str(), O_RDONLY | O_CLOEXEC | O_NOCTTY);
    }
    int saved = errno;
    ::close(dirfd);
    if (fd < 0) {
        error_ = "cannot open " + name + ": " + strerror(saved);
        return false;
    }
    return mapFd(fd, name);
}

bool MappedFile::mapFd(int fd, const std::string& name) {
    struct stat st{};
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        error_ = name + " is not a regular file";
        ::close(fd);
        return false;
    }
    size_ = static_cast<size_t>(st.st_size);
    if (size_ > 0) {
        void* map = mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
        if (map == MAP_FAILED) {
            error_ = "cannot map " + name + ": " + strerror(errno);
            size_ = 0;
            ::close(fd);
            return false;
        }
        data_ = map;
    }
    ::close(fd);  // the mapping stays valid
    open_ = true;
    error_.clear();
    return true;
}

void MappedFile::close() {
    if (data_) munmap(data_, size_);
    data_ = nullptr;
    size_ = 0;
    open_ = false;
}

}  // namespace canvas::scene
