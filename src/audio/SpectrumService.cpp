// Implementation of SpectrumService declared in SpectrumService.h.
#include "audio/SpectrumService.h"

#include "core/EventLoop.h"
#include "core/Log.h"

namespace canvas {

SpectrumService::SpectrumService(EventLoop& loop, std::unique_ptr<AudioCaptureBackend> backend)
    : loop_(loop), backend_(std::move(backend)) {
    std::string error;
    if (!writer_.create(error)) log::warn() << "audio spectrum unavailable: " << error;
    writer_.publish(AudioSpectrum{});
}

SpectrumService::~SpectrumService() {
    *alive_ = false;
    if (retryTimer_) loop_.cancelTimer(retryTimer_);
    stopCapture();
}

void SpectrumService::setEnabled(bool enabled) {
    enabled_ = enabled;
    update();
}

void SpectrumService::setDemand(const std::string& who, bool wanted) {
    if (wanted) demand_.insert(who);
    else demand_.erase(who);
    update();
}

void SpectrumService::update() {
    const bool want = enabled_ && !demand_.empty() && backend_ && writer_.ready();
    if (want && !capturing_ && !retryTimer_) startCapture();
    if (!want) {
        if (retryTimer_) {
            loop_.cancelTimer(retryTimer_);
            retryTimer_ = 0;
        }
        stopCapture();
    }
}

void SpectrumService::startCapture() {
    analyzer_.reset();
    std::string error;
    std::weak_ptr<bool> alive = alive_;
    const uint64_t generation = ++generation_;
    auto frames = [this](const float* f, size_t n) {
        analyzer_.feed(f, n, [this](const AudioSpectrum& s) { writer_.publish(s); });
    };
    auto failure = [this, alive, generation](const std::string& what) {
        // Backend thread: hand over to the loop; a failure of an older
        // capture is stale.
        loop_.post([this, alive, generation, what] {
            if (auto a = alive.lock(); a && *a && generation == generation_) onFailure(what);
        });
    };
    if (!backend_->start(frames, failure, error)) {
        onFailure(error);
        return;
    }
    capturing_ = true;
    log::info() << "audio capture started";
}

void SpectrumService::stopCapture() {
    if (!capturing_) return;
    backend_->stop();
    capturing_ = false;
    writer_.publish(AudioSpectrum{});
    log::info() << "audio capture stopped";
}

void SpectrumService::onFailure(const std::string& error) {
    log::warn() << "audio capture: " << error << "; retrying in " << kRetrySeconds << " s";
    stopCapture();
    if (retryTimer_) return;
    retryTimer_ = loop_.addTimer(uint64_t(kRetrySeconds) * 1000, [this] {
        retryTimer_ = 0;
        update();
    });
}

}  // namespace canvas
