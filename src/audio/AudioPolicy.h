// Single source of truth for wallpaper audio.
//
// Requirement: wallpapers with audio are muted by default. The policy holds
// the global default plus per wallpaper overrides (set through IPC). Backends
// query effectiveMuted() and are notified through InstanceManager when the
// policy changes. Note that scene wallpapers apply mute via the
// linux-wallpaperengine --silent flag, so changing their mute state requires
// an instance restart; InstanceManager handles that.
#pragma once

#include <map>
#include <string>

namespace canvas {

class AudioPolicy {
public:
    explicit AudioPolicy(bool defaultMuted = true, int volume = 15)
        : default_muted_(defaultMuted), volume_(volume) {}

    bool effectiveMuted(const std::string& wallpaperId) const {
        auto it = overrides_.find(wallpaperId);
        return it != overrides_.end() ? it->second : default_muted_;
    }
    int volume() const { return volume_; }

    void setDefaultMuted(bool muted) { default_muted_ = muted; }
    bool defaultMuted() const { return default_muted_; }

    // Per wallpaper override; empty id clears back to the default.
    void setOverride(const std::string& wallpaperId, bool muted) {
        overrides_[wallpaperId] = muted;
    }
    void clearOverride(const std::string& wallpaperId) {
        overrides_.erase(wallpaperId);
    }

private:
    bool default_muted_;
    int volume_;
    std::map<std::string, bool> overrides_;
};

}  // namespace canvas
