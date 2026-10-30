// Timed wallpaper rotation over the enabled wallpapers in config.
//
// Every `interval` seconds a random id is drawn from config "wallpapers"
// entries with enabled == true (Config::enabledIds) and pushed
// into InstanceManager as a runtime override, which reconciles instances the
// same way an IPC "set" would. Config is never rewritten by a tick: the
// picks live only in the override map (see InstanceManager::setOverride), so
// stopping rotation restores whatever the user had assigned.
//
// Scope: all rotating outputs show the SAME pick. config.playlistOutputs
// narrows which outputs rotate at all; when it is empty the override is
// installed under "*" so every output, including ones hotplugged between
// ticks, follows the rotation.
//
// Enabled ids that are missing from the library, unsupported, or on the
// broken blacklist are skipped at pick time, so a stale entry costs one
// skipped candidate rather than a blank screen. When only one usable
// wallpaper is enabled and scene.randomize_options is on, a tick relaunches
// it (InstanceManager::restartWallpaper) so a new random variant shows.
#pragma once

#include <cstdint>
#include <string>

#include "source/Playlist.h"

namespace canvas {

class Config;
class EventLoop;
class InstanceManager;

class Rotator {
public:
    Rotator(EventLoop& loop, InstanceManager& manager, Config& config);
    ~Rotator();

    // Rebuilds the pool from config.enabledIds() and arms the timer when it
    // is non empty and config.playlistInterval > 0. Safe to call repeatedly
    // (after any change to enabled wallpapers, interval or outputs); each
    // call re-arms from now. Always returns true (kept for callers).
    bool start();

    // Rebuilds the pool after enabled wallpapers changed without forcing a
    // pick: a running rotation keeps its schedule (re-armed from now when
    // rearm is true, e.g. after an interval change); an idle one starts,
    // picking at once only if nothing has been picked yet; an empty pool or
    // interval 0 cancels the timer but leaves the current wallpaper up.
    void refresh(bool rearm = false);

    // Cancels the timer and drops the overrides, so outputs fall back to
    // their config assignments on the next apply().
    void stop();

    // Picks and applies immediately, then restarts the interval so a manual
    // advance does not land right before a scheduled tick.
    void advance();

    bool active() const { return timer_ != 0; }
    size_t size() const { return playlist_.size(); }
    const std::string& current() const { return current_; }
    // Seconds until the next tick; 0 when inactive.
    uint64_t secondsRemaining() const;

private:
    void arm();
    void tick();

    EventLoop& loop_;
    InstanceManager& manager_;
    Config& config_;
    Playlist playlist_;
    std::string current_;   // id most recently applied by a tick
    uint64_t timer_ = 0;    // EventLoop timer id, 0 = not armed
    uint64_t dueAtMs_ = 0;  // monotonic ms when the armed timer fires
};

}  // namespace canvas
