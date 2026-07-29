// Implementation of Rotator declared in Rotator.h. See the header for the
// scoping and persistence rules.
#include "render/Rotator.h"

#include <chrono>

#include "config/Config.h"
#include "core/EventLoop.h"
#include "core/Log.h"
#include "render/InstanceManager.h"

namespace canvas {

namespace {

uint64_t nowMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
            .count());
}

}  // namespace

Rotator::Rotator(EventLoop& loop, InstanceManager& manager, Config& config)
    : loop_(loop), manager_(manager), config_(config) {}

Rotator::~Rotator() {
    if (timer_) loop_.cancelTimer(timer_);
}

bool Rotator::start() {
    if (timer_) {
        loop_.cancelTimer(timer_);
        timer_ = 0;
    }
    if (config_.playlistFile.empty()) {
        playlist_.clear();
        return true;
    }
    if (!playlist_.load(config_.playlistFile)) return false;
    if (config_.playlistInterval <= 0) {
        log::info() << "playlist loaded but rotation is off (interval 0)";
        return true;
    }
    if (playlist_.empty()) {
        log::warn() << "playlist " << config_.playlistFile
                    << " has no ids; rotation not started";
        return true;
    }
    log::info() << "rotating " << playlist_.size() << " wallpaper(s) every "
                << config_.playlistInterval << "s across "
                << (config_.playlistOutputs.empty()
                        ? std::string("all outputs")
                        : std::to_string(config_.playlistOutputs.size()) +
                              " output(s)");
    // Apply a first pick immediately: waiting a whole interval before
    // anything changes makes a fresh --rotate look broken.
    tick();
    return true;
}

void Rotator::stop() {
    if (timer_) {
        loop_.cancelTimer(timer_);
        timer_ = 0;
    }
    dueAtMs_ = 0;
    current_.clear();
    manager_.clearOverrides();
    manager_.apply();
}

void Rotator::advance() {
    if (playlist_.empty()) return;
    tick();
}

void Rotator::arm() {
    // advance() can reach here with a timer still armed; dropping the id
    // without cancelling would leak the timerfd and fire a stray tick.
    if (timer_) {
        loop_.cancelTimer(timer_);
        timer_ = 0;
    }
    uint64_t ms = static_cast<uint64_t>(config_.playlistInterval) * 1000;
    dueAtMs_ = nowMs() + ms;
    timer_ = loop_.addTimer(ms, [this] {
        timer_ = 0;  // one shot fired; tick re-arms
        tick();
    });
}

void Rotator::tick() {
    std::string id = playlist_.pick(
        current_, [this](const std::string& c) { return manager_.usable(c); });
    if (id.empty()) {
        log::warn() << "no usable wallpaper in playlist "
                    << config_.playlistFile << " (" << playlist_.size()
                    << " id(s) checked against the library)";
    } else if (id != current_) {
        current_ = id;
        // Every rotating output shows the same pick. With no explicit output
        // list the override goes under "*" so hotplugged outputs join the
        // rotation without waiting for the next tick.
        if (config_.playlistOutputs.empty()) {
            manager_.setOverride("*", id);
        } else {
            for (const auto& name : config_.playlistOutputs)
                manager_.setOverride(name, id);
        }
        log::info() << "rotated to wallpaper " << id;
        manager_.apply();
    }
    // Re-arm even when the pick failed so a library that appears later (or a
    // reload that fixes the ids) resumes rotating on its own.
    if (config_.playlistInterval > 0) arm();
}

uint64_t Rotator::secondsRemaining() const {
    if (!timer_) return 0;
    uint64_t now = nowMs();
    return dueAtMs_ > now ? (dueAtMs_ - now + 999) / 1000 : 0;
}

}  // namespace canvas
