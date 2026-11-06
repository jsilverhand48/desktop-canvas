// SoundOutput through libmpv (src/scene/audio/README.md section 5): one
// mpv player per sound layer, audio only, no config files or terminal,
// reading the layer's files from memory through mpv's stream callbacks
// (like VideoTexture), so packaged sounds are never written out. Tracks
// play as "desktopcanvassound://<index>"; end of file events are drained
// in takeEnded(). The bytes must outlive the output: `keep` holds their
// owners. Built only with libmpv (CANVAS_SCENE_HAVE_MPV); without it
// open() fails and available() is false.
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "scene/audio/SoundLayer.h"

struct mpv_handle;

namespace canvas::scene {

class SoundOutputMpv : public SoundOutput {
public:
    static bool available();
    static std::unique_ptr<SoundOutputMpv> open(std::vector<std::span<const uint8_t>> tracks,
                                                std::vector<std::shared_ptr<const void>> keep,
                                                std::string& error);
    ~SoundOutputMpv() override;

    void play(size_t index, bool repeat) override;
    void setPaused(bool paused) override;
    void stop() override;
    void setVolume(double percent, bool muted) override;
    bool takeEnded() override;

private:
    SoundOutputMpv() = default;

    std::vector<std::span<const uint8_t>> tracks_;
    std::vector<std::shared_ptr<const void>> keep_;
    mpv_handle* mpv_ = nullptr;
};

}  // namespace canvas::scene
