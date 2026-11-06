// The audio spectrum the daemon shares with its helpers, per
// src/scene/audio/README.md sections 2 and 3.
//
// AudioSpectrum holds one analysis: levels for 64, 32 and 16 bands per
// channel, low frequencies first. SpectrumWriter (the daemon) owns a memfd
// holding the latest one behind a seqlock, sealed so it can never change
// size; childFd() is a separate read only descriptor for helpers, which
// can therefore only map it read only. SpectrumReader (a helper) maps such
// a descriptor and copies a consistent spectrum out. One thread may
// publish at a time; any number of processes may read.
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace canvas {

struct AudioSpectrum {
    std::array<float, 64> left64{}, right64{};
    std::array<float, 32> left32{}, right32{};
    std::array<float, 16> left16{}, right16{};
    bool capturing = false;  // false: nothing is captured, every level is 0
};

class SpectrumWriter {
public:
    static constexpr uint32_t kMagic = 0x55414344;  // "DCAU"
    static constexpr uint32_t kVersion = 1;
    static constexpr size_t kSize = 4096;

    SpectrumWriter() = default;
    ~SpectrumWriter();
    SpectrumWriter(const SpectrumWriter&) = delete;
    SpectrumWriter& operator=(const SpectrumWriter&) = delete;

    // Creates and seals the memfd; false (with error) when the kernel
    // refuses (no memfd or sealing support).
    bool create(std::string& error);
    bool ready() const { return map_ != nullptr; }
    // A read only descriptor of the same memory for children (close on
    // exec; the spawner dups it into place); -1 before create().
    int childFd() const { return childFd_; }
    void publish(const AudioSpectrum& spectrum);

private:
    int fd_ = -1;
    int childFd_ = -1;
    void* map_ = nullptr;
};

class SpectrumReader {
public:
    SpectrumReader() = default;
    ~SpectrumReader();
    SpectrumReader(const SpectrumReader&) = delete;
    SpectrumReader& operator=(const SpectrumReader&) = delete;

    // Maps fd read only and takes ownership of it; false (with error) when
    // it is not a spectrum memory of this version.
    bool open(int fd, std::string& error);
    bool ready() const { return map_ != nullptr; }
    // The latest spectrum; silence when not open or after repeated torn
    // reads (a writer stuck mid write).
    AudioSpectrum read() const;

private:
    int fd_ = -1;
    const void* map_ = nullptr;
};

}  // namespace canvas
