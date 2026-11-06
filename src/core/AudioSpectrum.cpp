// Implementation of the shared spectrum declared in AudioSpectrum.h.
//
// Layout (little endian, src/scene/audio/README.md section 3): u32 magic,
// u32 version, u32 sequence, u32 flags (bit 0 capturing), then the floats
// of left64, right64, left32, right32, left16 and right16. Every word is
// read and written with atomic builtins so the seqlock has no data races.
#include "core/AudioSpectrum.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <bit>
#include <cerrno>
#include <cstdio>
#include <cstring>

#ifndef F_SEAL_FUTURE_WRITE
#define F_SEAL_FUTURE_WRITE 0x0010
#endif

namespace canvas {

namespace {

constexpr size_t kHeaderWords = 4;
constexpr size_t kFloats = 64 * 2 + 32 * 2 + 16 * 2;
static_assert((kHeaderWords + kFloats) * 4 <= SpectrumWriter::kSize);

// The arrays in layout order.
template <class S, class F>
void eachArray(S& s, F&& f) {
    f(s.left64.data(), s.left64.size());
    f(s.right64.data(), s.right64.size());
    f(s.left32.data(), s.left32.size());
    f(s.right32.data(), s.right32.size());
    f(s.left16.data(), s.left16.size());
    f(s.right16.data(), s.right16.size());
}

}  // namespace

SpectrumWriter::~SpectrumWriter() {
    if (map_) munmap(map_, kSize);
    if (childFd_ >= 0) close(childFd_);
    if (fd_ >= 0) close(fd_);
}

bool SpectrumWriter::create(std::string& error) {
    fd_ = memfd_create("desktop-canvas-audio", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd_ < 0) {
        error = std::string("memfd_create: ") + std::strerror(errno);
        return false;
    }
    if (ftruncate(fd_, kSize) != 0) {
        error = std::string("ftruncate: ") + std::strerror(errno);
        return false;
    }
    void* map = mmap(nullptr, kSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (map == MAP_FAILED) {
        error = std::string("mmap: ") + std::strerror(errno);
        return false;
    }
    map_ = map;
    auto* words = static_cast<uint32_t*>(map_);
    __atomic_store_n(&words[0], kMagic, __ATOMIC_RELAXED);
    __atomic_store_n(&words[1], kVersion, __ATOMIC_RELAXED);
    __atomic_store_n(&words[2], 0u, __ATOMIC_RELEASE);
    // No resizing ever; no writable mapping or write() after this one.
    // Kernels before 5.1 lack the future write seal: the read only
    // descriptor below still keeps helpers from writing.
    if (fcntl(fd_, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_FUTURE_WRITE | F_SEAL_SEAL) != 0)
        fcntl(fd_, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL);
    char path[64];
    std::snprintf(path, sizeof path, "/proc/self/fd/%d", fd_);
    childFd_ = open(path, O_RDONLY | O_CLOEXEC);
    if (childFd_ < 0) childFd_ = fcntl(fd_, F_DUPFD_CLOEXEC, 0);  // sealed: still read only for others
    if (childFd_ < 0) {
        error = std::string("child descriptor: ") + std::strerror(errno);
        return false;
    }
    return true;
}

void SpectrumWriter::publish(const AudioSpectrum& spectrum) {
    if (!map_) return;
    auto* words = static_cast<uint32_t*>(map_);
    const uint32_t seq = __atomic_load_n(&words[2], __ATOMIC_RELAXED);
    __atomic_store_n(&words[2], seq + 1, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(&words[3], spectrum.capturing ? 1u : 0u, __ATOMIC_RELAXED);
    size_t at = kHeaderWords;
    eachArray(spectrum, [&](const float* v, size_t n) {
        for (size_t i = 0; i < n; i++) __atomic_store_n(&words[at++], std::bit_cast<uint32_t>(v[i]), __ATOMIC_RELAXED);
    });
    __atomic_store_n(&words[2], seq + 2, __ATOMIC_RELEASE);
}

SpectrumReader::~SpectrumReader() {
    if (map_) munmap(const_cast<void*>(map_), SpectrumWriter::kSize);
    if (fd_ >= 0) close(fd_);
}

bool SpectrumReader::open(int fd, std::string& error) {
    fd_ = fd;
    struct stat st {};
    if (fstat(fd, &st) != 0 || st.st_size < off_t(SpectrumWriter::kSize)) {
        error = "audio descriptor is not a spectrum memory";
        return false;
    }
    void* map = mmap(nullptr, SpectrumWriter::kSize, PROT_READ, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        error = std::string("mmap: ") + std::strerror(errno);
        return false;
    }
    const auto* words = static_cast<const uint32_t*>(map);
    if (__atomic_load_n(&words[0], __ATOMIC_RELAXED) != SpectrumWriter::kMagic ||
        __atomic_load_n(&words[1], __ATOMIC_RELAXED) != SpectrumWriter::kVersion) {
        munmap(map, SpectrumWriter::kSize);
        error = "audio descriptor has another layout";
        return false;
    }
    map_ = map;
    return true;
}

AudioSpectrum SpectrumReader::read() const {
    AudioSpectrum out;
    if (!map_) return out;
    const auto* words = static_cast<const uint32_t*>(map_);
    for (int attempt = 0; attempt < 64; attempt++) {
        const uint32_t before = __atomic_load_n(&words[2], __ATOMIC_ACQUIRE);
        if (before & 1) continue;
        out.capturing = (__atomic_load_n(&words[3], __ATOMIC_RELAXED) & 1) != 0;
        size_t at = kHeaderWords;
        eachArray(out, [&](float* v, size_t n) {
            for (size_t i = 0; i < n; i++)
                v[i] = std::bit_cast<float>(__atomic_load_n(&words[at++], __ATOMIC_RELAXED));
        });
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (__atomic_load_n(&words[2], __ATOMIC_RELAXED) == before) return out;
    }
    return AudioSpectrum{};
}

}  // namespace canvas
