// Implementation of SpectrumAnalyzer declared in SpectrumAnalyzer.h.
#include "audio/SpectrumAnalyzer.h"

#include <algorithm>
#include <cmath>

namespace canvas {

namespace {

constexpr double kPi = 3.14159265358979323846;

}  // namespace

double SpectrumAnalyzer::bandEdge(int i) {
    return kLowHz * std::pow(kHighHz / kLowHz, double(i) / kBands);
}

SpectrumAnalyzer::SpectrumAnalyzer() {
    // Periodic Hann window; its sum turns bin magnitudes into amplitudes.
    for (int n = 0; n < kWindow; n++) {
        window_[size_t(n)] = float(0.5 - 0.5 * std::cos(2 * kPi * n / kWindow));
        windowSum_ += window_[size_t(n)];
    }
    buffer_.resize(kWindow);
    twiddles_.resize(kWindow / 2);
    for (int k = 0; k < kWindow / 2; k++) twiddles_[size_t(k)] = std::polar(1.0, -2 * kPi * k / kWindow);
    int bits = 0;
    while ((1 << bits) < kWindow) bits++;
    reversed_.resize(kWindow);
    for (size_t i = 0; i < size_t(kWindow); i++) {
        size_t r = 0;
        for (int b = 0; b < bits; b++)
            if (i & (size_t(1) << b)) r |= size_t(1) << (bits - 1 - b);
        reversed_[i] = r;
    }
    const double binHz = double(kRate) / kWindow;
    for (int b = 0; b < kBands; b++) {
        const double lo = bandEdge(b), hi = bandEdge(b + 1);
        Band& band = bands_[size_t(b)];
        band.first = int(std::ceil(lo / binHz));
        band.last = int(std::ceil(hi / binHz)) - 1;
        band.centreBin = std::sqrt(lo * hi) / binHz;
    }
}

void SpectrumAnalyzer::reset() {
    left_.fill(0);
    right_.fill(0);
    head_ = filled_ = sinceHop_ = 0;
    levelLeft_.fill(0);
    levelRight_.fill(0);
    spectrum_ = AudioSpectrum{};
}

void SpectrumAnalyzer::feed(const float* frames, size_t count,
                            const std::function<void(const AudioSpectrum&)>& onSpectrum) {
    for (size_t i = 0; i < count; i++) {
        const float l = frames[2 * i], r = frames[2 * i + 1];
        left_[head_] = std::isfinite(l) ? l : 0.0f;
        right_[head_] = std::isfinite(r) ? r : 0.0f;
        head_ = (head_ + 1) % size_t(kWindow);
        filled_ = std::min(filled_ + 1, size_t(kWindow));
        if (++sinceHop_ == size_t(kHop)) {
            sinceHop_ = 0;
            analyse();
            if (onSpectrum) onSpectrum(spectrum_);
        }
    }
}

void SpectrumAnalyzer::analyse() {
    // Both channels in one complex FFT: left real, right imaginary, oldest
    // frame first, bit reversed for the in place transform.
    for (size_t n = 0; n < size_t(kWindow); n++) {
        const size_t at = (head_ + n) % size_t(kWindow);
        buffer_[reversed_[n]] = {double(left_[at]) * window_[n], double(right_[at]) * window_[n]};
    }
    for (size_t size = 2; size <= size_t(kWindow); size *= 2) {
        const size_t half = size / 2, step = size_t(kWindow) / size;
        for (size_t start = 0; start < size_t(kWindow); start += size)
            for (size_t k = 0; k < half; k++) {
                const std::complex<double> t = twiddles_[k * step] * buffer_[start + k + half];
                buffer_[start + k + half] = buffer_[start + k] - t;
                buffer_[start + k] += t;
            }
    }
    // Separate the channels: L[k] = (Z[k] + conj Z[N-k]) / 2, R[k] = (Z[k]
    // - conj Z[N-k]) / 2i; amplitude = 2 |X| / sum of the window.
    std::array<double, kWindow / 2 + 1> ampL{}, ampR{};
    for (size_t k = 0; k <= size_t(kWindow) / 2; k++) {
        const std::complex<double> z = buffer_[k];
        const std::complex<double> zc = std::conj(buffer_[(size_t(kWindow) - k) % size_t(kWindow)]);
        const std::complex<double> l = (z + zc) * 0.5;
        const std::complex<double> r = (z - zc) * std::complex<double>(0, -0.5);
        ampL[k] = 2 * std::abs(l) / windowSum_;
        ampR[k] = 2 * std::abs(r) / windowSum_;
    }
    const double decay = std::exp(-double(kHop) / kRate / kReleaseSeconds);
    auto level = [](double amplitude) {
        return amplitude > 0 ? std::max(0.0, 1 + 20 * std::log10(amplitude) / kFloorDb) : 0.0;
    };
    auto bandAmplitude = [&](const std::array<double, kWindow / 2 + 1>& amp, const Band& band) {
        if (band.first <= band.last) {
            double a = 0;
            for (int k = band.first; k <= band.last && k <= kWindow / 2; k++) a = std::max(a, amp[size_t(k)]);
            return a;
        }
        const double c = std::min(band.centreBin, double(kWindow) / 2 - 1);
        const size_t k0 = size_t(c);
        const double t = c - double(k0);
        return amp[k0] * (1 - t) + amp[k0 + 1] * t;
    };
    for (size_t b = 0; b < size_t(kBands); b++) {
        levelLeft_[b] = std::max(level(bandAmplitude(ampL, bands_[b])), levelLeft_[b] * decay);
        levelRight_[b] = std::max(level(bandAmplitude(ampR, bands_[b])), levelRight_[b] * decay);
        spectrum_.left64[b] = float(levelLeft_[b]);
        spectrum_.right64[b] = float(levelRight_[b]);
    }
    for (size_t b = 0; b < 32; b++) {
        spectrum_.left32[b] = (spectrum_.left64[2 * b] + spectrum_.left64[2 * b + 1]) / 2;
        spectrum_.right32[b] = (spectrum_.right64[2 * b] + spectrum_.right64[2 * b + 1]) / 2;
    }
    for (size_t b = 0; b < 16; b++) {
        spectrum_.left16[b] = (spectrum_.left32[2 * b] + spectrum_.left32[2 * b + 1]) / 2;
        spectrum_.right16[b] = (spectrum_.right32[2 * b] + spectrum_.right32[2 * b + 1]) / 2;
    }
    spectrum_.capturing = true;
}

}  // namespace canvas
