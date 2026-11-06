// The daemon's one audio capture for every helper, per
// src/scene/audio/README.md sections 2 and 3.
//
// SpectrumService owns the shared spectrum memory (SpectrumWriter) and
// runs the capture while it is enabled (config audio.processing) and some
// helper wants it: helpers report through setDemand(), keyed by their
// tag, true only while their scene reads the spectrum and they are
// running unpaused. The capture backend delivers stereo frames on its own
// thread; a SpectrumAnalyzer turns them into levels published straight
// from that thread. When the capture stops, or fails (no sound server),
// the memory holds silence; failed captures are retried every
// kRetrySeconds while still wanted.
//
// AudioCaptureBackend is the seam for the sound server: PulseCapture
// (PulseAudio protocol, also served by PipeWire) in the daemon, fakes in
// tests. Main thread only, except the backend's frame callback.
#pragma once

#include <functional>
#include <memory>
#include <set>
#include <string>

#include "audio/SpectrumAnalyzer.h"
#include "core/AudioSpectrum.h"

namespace canvas {

class EventLoop;

class AudioCaptureBackend {
public:
    // Interleaved stereo float frames at SpectrumAnalyzer::kRate.
    using Frames = std::function<void(const float* frames, size_t count)>;
    virtual ~AudioCaptureBackend() = default;
    // Starts capturing what the default output plays. Frames and failure
    // arrive on the backend's thread; failure (the server went away or
    // refused) ends the capture. False (with error) when it cannot even
    // start.
    virtual bool start(Frames frames, std::function<void(const std::string&)> failure, std::string& error) = 0;
    // Stops; no callback runs after it returns.
    virtual void stop() = 0;
};

class SpectrumService {
public:
    static constexpr int kRetrySeconds = 5;

    // backend may be null: the memory exists, every level stays 0.
    SpectrumService(EventLoop& loop, std::unique_ptr<AudioCaptureBackend> backend);
    ~SpectrumService();
    SpectrumService(const SpectrumService&) = delete;
    SpectrumService& operator=(const SpectrumService&) = delete;

    // The read only descriptor helpers get at spawn; -1 when the memory
    // could not be made.
    int childFd() const { return writer_.childFd(); }
    void setEnabled(bool enabled);
    bool enabled() const { return enabled_; }
    void setDemand(const std::string& who, bool wanted);
    bool capturing() const { return capturing_; }

private:
    void update();
    void startCapture();
    void stopCapture();
    void onFailure(const std::string& error);

    EventLoop& loop_;
    std::unique_ptr<AudioCaptureBackend> backend_;
    SpectrumWriter writer_;
    SpectrumAnalyzer analyzer_;  // used by the capture thread while capturing
    std::set<std::string> demand_;
    bool enabled_ = true;
    bool capturing_ = false;
    uint64_t retryTimer_ = 0;
    uint64_t generation_ = 0;  // of the current capture; older failures are ignored
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace canvas
