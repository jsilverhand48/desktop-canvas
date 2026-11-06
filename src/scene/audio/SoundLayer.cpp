// Implementation of SoundPlayer declared in SoundLayer.h.
#include "scene/audio/SoundLayer.h"

#include <algorithm>
#include <cmath>

namespace canvas::scene {

SoundPlayer::Mode SoundPlayer::modeFromName(const std::string& name) {
    if (name == "single") return Mode::Single;
    if (name == "random") return Mode::Random;
    return Mode::Loop;
}

SoundPlayer::SoundPlayer(Mode mode, size_t tracks, double minTime, double maxTime,
                         std::unique_ptr<SoundOutput> output, uint64_t seed)
    : mode_(mode),
      tracks_(tracks),
      minTime_(std::max(0.0, std::min(minTime, maxTime))),
      maxTime_(std::max(0.0, std::max(minTime, maxTime))),
      output_(output ? std::move(output) : std::make_unique<SilentSoundOutput>()),
      rng_(seed ? seed : 0x9E3779B97F4A7C15ull) {}

size_t SoundPlayer::randomTrack() {
    // xorshift64*: deterministic for a seed.
    rng_ ^= rng_ >> 12;
    rng_ ^= rng_ << 25;
    rng_ ^= rng_ >> 27;
    return size_t((rng_ * 0x2545F4914F6CDD1Dull) >> 33) % std::max<size_t>(1, tracks_);
}

double SoundPlayer::randomWait() {
    const double u = double(randomTrack() % 1000) / 999.0;
    return minTime_ + (maxTime_ - minTime_) * u;
}

void SoundPlayer::begin(size_t index) {
    track_ = index;
    output_->play(index, mode_ == Mode::Loop && tracks_ == 1);
    output_->setPaused(hostPaused_);
    state_ = State::Playing;
}

void SoundPlayer::start(bool startSilent) {
    if (!startSilent) play();
}

void SoundPlayer::play() {
    if (tracks_ == 0) return;
    if (state_ == State::Paused) {
        state_ = State::Playing;  // resume where it was
        output_->setPaused(hostPaused_);
        return;
    }
    if (state_ == State::Stopped) begin(mode_ == Mode::Random ? randomTrack() : 0);
}

void SoundPlayer::pause() {
    if (state_ == State::Playing) {
        output_->setPaused(true);
        state_ = State::Paused;
    } else if (state_ == State::Waiting) {
        state_ = State::Stopped;  // nothing is sounding; play() starts the next
    }
}

void SoundPlayer::stop() {
    if (state_ != State::Stopped) output_->stop();
    state_ = State::Stopped;
    track_ = 0;
}

bool SoundPlayer::isPlaying() const { return state_ == State::Playing || state_ == State::Waiting; }

void SoundPlayer::setHostPaused(bool paused) {
    if (paused == hostPaused_) return;
    hostPaused_ = paused;
    if (state_ == State::Playing) output_->setPaused(paused);
}

void SoundPlayer::update(double dt, float layerVolume, int wallpaperVolume, bool muted) {
    const double percent =
        std::clamp(double(wallpaperVolume), 0.0, 100.0) * std::cbrt(std::clamp(double(layerVolume), 0.0, 4.0));
    if (percent != volume_ || muted != muted_) {
        volume_ = percent;
        muted_ = muted;
        output_->setVolume(percent, muted);
    }
    if (hostPaused_) return;
    if (state_ == State::Playing && output_->takeEnded()) {
        switch (mode_) {
            case Mode::Loop:
                begin((track_ + 1) % tracks_);
                break;
            case Mode::Single:
                if (track_ + 1 < tracks_) begin(track_ + 1);
                else state_ = State::Stopped;
                break;
            case Mode::Random:
                wait_ = randomWait();
                state_ = State::Waiting;
                break;
        }
    } else if (state_ == State::Waiting) {
        wait_ -= dt;
        if (wait_ <= 0) begin(randomTrack());
    }
}

}  // namespace canvas::scene
