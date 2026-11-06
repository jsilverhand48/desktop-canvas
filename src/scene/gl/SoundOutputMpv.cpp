// Implementation of SoundOutputMpv declared in SoundOutputMpv.h.
#include "scene/gl/SoundOutputMpv.h"

#ifdef CANVAS_SCENE_HAVE_MPV

#include <mpv/client.h>
#include <mpv/stream_cb.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace canvas::scene {

namespace {

constexpr const char* kProtocol = "desktopcanvassound";

// One open stream over a track's bytes, read from mpv's threads.
struct Cursor {
    std::span<const uint8_t> bytes;
    size_t pos = 0;
};

int64_t streamRead(void* cookie, char* buf, uint64_t nbytes) {
    auto* c = static_cast<Cursor*>(cookie);
    const size_t n = size_t(std::min<uint64_t>(nbytes, c->bytes.size() - c->pos));
    std::memcpy(buf, c->bytes.data() + c->pos, n);
    c->pos += n;
    return int64_t(n);
}

int64_t streamSeek(void* cookie, int64_t offset) {
    auto* c = static_cast<Cursor*>(cookie);
    if (offset < 0 || uint64_t(offset) > c->bytes.size()) return MPV_ERROR_GENERIC;
    c->pos = size_t(offset);
    return offset;
}

int64_t streamSize(void* cookie) { return int64_t(static_cast<Cursor*>(cookie)->bytes.size()); }

void streamClose(void* cookie) { delete static_cast<Cursor*>(cookie); }

// uri: "desktopcanvassound://<index>".
int streamOpen(void* user, char* uri, mpv_stream_cb_info* info) {
    const auto& tracks = *static_cast<const std::vector<std::span<const uint8_t>>*>(user);
    const char* at = uri ? std::strstr(uri, "://") : nullptr;
    if (!at) return MPV_ERROR_LOADING_FAILED;
    char* end = nullptr;
    const unsigned long index = std::strtoul(at + 3, &end, 10);
    if (end == at + 3 || index >= tracks.size()) return MPV_ERROR_LOADING_FAILED;
    info->cookie = new Cursor{tracks[index]};
    info->read_fn = streamRead;
    info->seek_fn = streamSeek;
    info->size_fn = streamSize;
    info->close_fn = streamClose;
    return 0;
}

}  // namespace

bool SoundOutputMpv::available() { return true; }

std::unique_ptr<SoundOutputMpv> SoundOutputMpv::open(std::vector<std::span<const uint8_t>> tracks,
                                                     std::vector<std::shared_ptr<const void>> keep,
                                                     std::string& error) {
    std::unique_ptr<SoundOutputMpv> s(new SoundOutputMpv());
    s->tracks_ = std::move(tracks);
    s->keep_ = std::move(keep);
    s->mpv_ = mpv_create();
    if (!s->mpv_) {
        error = "mpv_create failed";
        return nullptr;
    }
    for (auto [name, value] : {std::pair{"config", "no"}, {"terminal", "no"}, {"msg-level", "all=error"},
                               {"video", "no"}, {"audio-display", "no"}, {"idle", "yes"}, {"cache", "no"},
                               {"input-default-bindings", "no"}, {"ytdl", "no"}, {"load-scripts", "no"},
                               {"audio-client-name", "desktop-canvas"}, {"volume", "0"}})
        mpv_set_option_string(s->mpv_, name, value);
    if (mpv_initialize(s->mpv_) < 0) {
        error = "mpv_initialize failed";
        return nullptr;
    }
    if (mpv_stream_cb_add_ro(s->mpv_, kProtocol, &s->tracks_, streamOpen) < 0) {
        error = "mpv stream callbacks failed";
        return nullptr;
    }
    return s;
}

SoundOutputMpv::~SoundOutputMpv() {
    if (mpv_) mpv_terminate_destroy(mpv_);  // closes the streams first
}

void SoundOutputMpv::play(size_t index, bool repeat) {
    if (index >= tracks_.size()) return;
    mpv_set_property_string(mpv_, "loop-file", repeat ? "inf" : "no");
    const std::string url = std::string(kProtocol) + "://" + std::to_string(index);
    const char* cmd[] = {"loadfile", url.c_str(), "replace", nullptr};
    mpv_command(mpv_, cmd);
    mpv_set_property_string(mpv_, "pause", "no");
    takeEnded();  // an end of the previous track is no longer news
}

void SoundOutputMpv::setPaused(bool paused) { mpv_set_property_string(mpv_, "pause", paused ? "yes" : "no"); }

void SoundOutputMpv::stop() {
    const char* cmd[] = {"stop", nullptr};
    mpv_command(mpv_, cmd);
    takeEnded();
}

void SoundOutputMpv::setVolume(double percent, bool muted) {
    mpv_set_property(mpv_, "volume", MPV_FORMAT_DOUBLE, &percent);
    mpv_set_property_string(mpv_, "mute", muted ? "yes" : "no");
}

bool SoundOutputMpv::takeEnded() {
    bool ended = false;
    while (true) {
        mpv_event* e = mpv_wait_event(mpv_, 0);
        if (!e || e->event_id == MPV_EVENT_NONE) break;
        if (e->event_id == MPV_EVENT_END_FILE) {
            const auto* end = static_cast<const mpv_event_end_file*>(e->data);
            if (end && (end->reason == MPV_END_FILE_REASON_EOF || end->reason == MPV_END_FILE_REASON_ERROR))
                ended = true;
        }
    }
    return ended;
}

}  // namespace canvas::scene

#else  // without libmpv

namespace canvas::scene {

bool SoundOutputMpv::available() { return false; }

std::unique_ptr<SoundOutputMpv> SoundOutputMpv::open(std::vector<std::span<const uint8_t>>,
                                                     std::vector<std::shared_ptr<const void>>, std::string& error) {
    error = "sound layers need a build with libmpv";
    return nullptr;
}

SoundOutputMpv::~SoundOutputMpv() = default;
void SoundOutputMpv::play(size_t, bool) {}
void SoundOutputMpv::setPaused(bool) {}
void SoundOutputMpv::stop() {}
void SoundOutputMpv::setVolume(double, bool) {}
bool SoundOutputMpv::takeEnded() { return false; }

}  // namespace canvas::scene

#endif
