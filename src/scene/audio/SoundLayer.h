// Sound layers, per src/scene/audio/README.md section 5: the playback
// modes, start silent, script control and volume of one sound object.
//
// SoundPlayer decides what plays when; a SoundOutput plays it (libmpv in
// the renderer, scene/gl/SoundOutputMpv.h; silent outputs for screenshots
// and builds without libmpv; fakes in tests). update() runs once per
// frame advance with the layer's current volume (the field may be
// animated or bound) and the wallpaper's volume and mute. No GPU, no
// threads; deterministic for a seed.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace canvas::scene {

class SoundOutput {
public:
    virtual ~SoundOutput() = default;
    // Starts track `index` of the layer's list from its beginning; with
    // repeat it plays again without a gap, forever.
    virtual void play(size_t index, bool repeat) = 0;
    virtual void setPaused(bool paused) = 0;
    virtual void stop() = 0;  // silent and rewound
    // percent on mpv's scale (0..100, cubic); muted silences without
    // losing the volume.
    virtual void setVolume(double percent, bool muted) = 0;
    // True once after the current track ended by itself.
    virtual bool takeEnded() = 0;
};

// An output that plays nothing and never ends: screenshots, builds
// without libmpv.
class SilentSoundOutput : public SoundOutput {
public:
    void play(size_t, bool) override {}
    void setPaused(bool) override {}
    void stop() override {}
    void setVolume(double, bool) override {}
    bool takeEnded() override { return false; }
};

class SoundPlayer {
public:
    enum class Mode { Loop, Single, Random };
    static Mode modeFromName(const std::string& name);  // unknown: Loop

    SoundPlayer(Mode mode, size_t tracks, double minTime, double maxTime, std::unique_ptr<SoundOutput> output,
                uint64_t seed);

    // At load: plays unless the layer starts silent.
    void start(bool startSilent);
    // ISoundLayer (section 5): play starts or resumes, pause holds, stop
    // ends and rewinds.
    void play();
    void pause();
    void stop();
    bool isPlaying() const;  // a track playing, or waiting between random ones
    // The wallpaper paused (fullscreen pause, every output covered).
    void setHostPaused(bool paused);
    // Once per advance: end of track handling, the random wait, volume
    // (layerVolume 0..1 times the wallpaper's percent).
    void update(double dt, float layerVolume, int wallpaperVolume, bool muted);
    size_t track() const { return track_; }

private:
    enum class State { Stopped, Playing, Paused, Waiting };
    void begin(size_t index);
    size_t randomTrack();
    double randomWait();

    Mode mode_;
    size_t tracks_;
    double minTime_, maxTime_;
    std::unique_ptr<SoundOutput> output_;
    uint64_t rng_;
    State state_ = State::Stopped;
    bool hostPaused_ = false;
    size_t track_ = 0;
    double wait_ = 0;
    double volume_ = -1;
    bool muted_ = false;
};

}  // namespace canvas::scene
