// What media players play, for script media events, per
// src/scene/audio/README.md section 7.
//
// MediaInfo is the daemon's "media" message (process/HelperProtocol.h)
// read back: integration on or off, playback state, the track's
// properties, its local album art file and the timeline (the serial
// counts timeline updates). albumArtColors() turns album art into the
// colours MediaThumbnailEvent carries: k-means with k = 3 over the image
// reduced to 64 x 64, clusters largest first; the text colour is the
// secondary or tertiary one when its WCAG contrast ratio with the primary
// is at least 4.5, else black or white; the high contrast colour is black
// or white, whichever contrasts more. No GPU, deterministic.
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "scene/assets/TextureDecode.h"
#include "scene/model/Field.h"

namespace canvas::scene {

struct MediaInfo {
    bool enabled = false;
    int state = 0;  // MediaPlaybackEvent: 0 stopped, 1 playing, 2 paused
    std::string title, artist, subTitle, albumTitle, albumArtist, genres, contentType;
    std::string artPath;  // local album art file; empty: none
    double position = 0, duration = 0;
    uint64_t timelineSerial = 0;  // grows with every timeline update

    static MediaInfo fromJson(const nlohmann::json& message);
    bool sameProperties(const MediaInfo& o) const;
};

struct AlbumArtColors {
    bool hasThumbnail = false;
    Vec3 primary{0, 0, 0}, secondary{0, 0, 0}, tertiary{0, 0, 0}, text{1, 1, 1}, highContrast{1, 1, 1};
};

// From a decoded RGBA8 image; hasThumbnail false for an empty one.
AlbumArtColors albumArtColors(const DecodedImage& image);
// WCAG 2 contrast ratio of two sRGB colours (components 0..1).
double contrastRatio(const Vec3& a, const Vec3& b);

}  // namespace canvas::scene
