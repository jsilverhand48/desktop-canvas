// Turns captured stereo audio into AudioSpectrum levels, per
// src/scene/audio/README.md section 2: every kHop frames (60 times a
// second at 48 kHz) the newest kWindow frames of each channel go through
// a Hann window and one FFT (both channels packed into one complex
// transform), then into 64 logarithmic bands from 40 Hz to 16 kHz, dB
// levels (-60 dBFS reads 0, a full scale sine 1), a 100 ms release, and
// the 32 and 16 band means. No I/O, no threads: whoever captures feeds it.
#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <functional>
#include <vector>

#include "core/AudioSpectrum.h"

namespace canvas {

class SpectrumAnalyzer {
public:
    static constexpr int kRate = 48000;
    static constexpr int kWindow = 2048;
    static constexpr int kHop = 800;
    static constexpr int kBands = 64;
    static constexpr double kLowHz = 40, kHighHz = 16000;
    static constexpr double kFloorDb = 60;      // dB below full scale that reads 0
    static constexpr double kReleaseSeconds = 0.1;

    SpectrumAnalyzer();

    // Forgets the audio seen so far and every level.
    void reset();
    // Interleaved stereo frames (left, right); calls onSpectrum after each
    // completed hop with the levels then, capturing set.
    void feed(const float* frames, size_t count,
              const std::function<void(const AudioSpectrum&)>& onSpectrum);
    const AudioSpectrum& spectrum() const { return spectrum_; }
    // A band's edges in Hz (tests).
    static double bandEdge(int i);

private:
    void analyse();

    std::array<float, kWindow> window_{};
    std::array<float, kWindow> left_{}, right_{};  // ring of the newest frames
    size_t head_ = 0;     // next write position in the rings
    size_t filled_ = 0;   // frames seen, up to kWindow
    size_t sinceHop_ = 0;
    double windowSum_ = 0;
    struct Band {
        int first = 0, last = -1;  // bins inside, last < first when none
        double centreBin = 0;      // for bands narrower than a bin
    };
    std::array<Band, kBands> bands_{};
    std::vector<std::complex<double>> buffer_;
    std::vector<std::complex<double>> twiddles_;
    std::vector<size_t> reversed_;
    std::array<double, kBands> levelLeft_{}, levelRight_{};
    AudioSpectrum spectrum_;
};

}  // namespace canvas
