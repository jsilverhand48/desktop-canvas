// Implementation of VideoTexture declared in VideoTexture.h.
#include "scene/gl/VideoTexture.h"

#ifdef CANVAS_SCENE_HAVE_MPV

#include <epoxy/egl.h>
#include <mpv/client.h>
#include <mpv/render_gl.h>
#include <mpv/stream_cb.h>

#include <algorithm>
#include <cstring>

#include "core/Log.h"

namespace canvas::scene {

namespace {

void* procAddress(void*, const char* name) {
    return reinterpret_cast<void*>(eglGetProcAddress(name));
}

// One open stream over the video's bytes (mpv may open it more than
// once); mpv reads, seeks and closes it from its own threads.
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

int streamOpen(void* user, char*, mpv_stream_cb_info* info) {
    info->cookie = new Cursor{*static_cast<std::span<const uint8_t>*>(user)};
    info->read_fn = streamRead;
    info->seek_fn = streamSeek;
    info->size_fn = streamSize;
    info->close_fn = streamClose;
    return 0;
}

}  // namespace

bool VideoTexture::available() { return true; }

std::unique_ptr<VideoTexture> VideoTexture::open(std::span<const uint8_t> mp4,
                                                 std::shared_ptr<const void> keep,
                                                 std::string& error) {
    std::unique_ptr<VideoTexture> v(new VideoTexture());
    v->bytes_ = mp4;
    v->keep_ = std::move(keep);
    v->mpv_ = mpv_create();
    if (!v->mpv_) {
        error = "mpv_create failed";
        return nullptr;
    }
    for (auto [name, value] : {std::pair{"config", "no"}, {"terminal", "no"}, {"msg-level", "all=error"},
                               {"vo", "libmpv"}, {"audio", "no"}, {"aid", "no"}, {"sid", "no"},
                               {"loop-file", "inf"}, {"hwdec", "no"}, {"cache", "no"},
                               {"input-default-bindings", "no"}, {"osc", "no"},
                               // A texture needs little read ahead, and frame threads
                               // each hold whole frames: by default one per core.
                               {"demuxer-max-bytes", "16MiB"}, {"demuxer-max-back-bytes", "1MiB"},
                               {"vd-lavc-threads", "2"}})
        mpv_set_option_string(v->mpv_, name, value);
    if (mpv_initialize(v->mpv_) < 0) {
        error = "mpv_initialize failed";
        return nullptr;
    }
    mpv_opengl_init_params gl{procAddress, nullptr};
    int advanced = 0;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl},
        {MPV_RENDER_PARAM_ADVANCED_CONTROL, &advanced},
        {MPV_RENDER_PARAM_INVALID, nullptr}};
    if (mpv_render_context_create(&v->render_, v->mpv_, params) < 0) {
        error = "mpv render context failed";
        return nullptr;
    }
    mpv_render_context_set_update_callback(v->render_, onUpdate, v.get());
    if (mpv_stream_cb_add_ro(v->mpv_, "desktopcanvas", &v->bytes_, streamOpen) < 0) {
        error = "mpv stream callbacks failed";
        return nullptr;
    }
    const char* cmd[] = {"loadfile", "desktopcanvas://video", nullptr};
    if (mpv_command(v->mpv_, cmd) < 0) {
        error = "mpv cannot load the video";
        return nullptr;
    }
    return v;
}

VideoTexture::~VideoTexture() {
    if (render_) mpv_render_context_free(render_);  // before the handle
    if (mpv_) mpv_terminate_destroy(mpv_);  // closes the streams first
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (texture_) glDeleteTextures(1, &texture_);
}

void VideoTexture::onUpdate(void* self) {
    static_cast<VideoTexture*>(self)->pending_ = true;
}

bool VideoTexture::update() {
    if (!render_ || !pending_.exchange(false)) return false;
    if (!(mpv_render_context_update(render_) & MPV_RENDER_UPDATE_FRAME)) return false;
    int64_t w = 0, h = 0;
    mpv_get_property(mpv_, "dwidth", MPV_FORMAT_INT64, &w);
    mpv_get_property(mpv_, "dheight", MPV_FORMAT_INT64, &h);
    if (w <= 0 || h <= 0) return false;
    bool resized = false;
    if (int(w) != width_ || int(h) != height_ || !texture_) {
        width_ = int(w);
        height_ = int(h);
        if (!texture_) glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (!fbo_) glGenFramebuffers(1, &fbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_, 0);
        resized = true;
    }
    // Row 0 holds the top of the picture, like every other texture.
    mpv_opengl_fbo target{int(fbo_), width_, height_, 0};
    int flip = 0;
    mpv_render_param params[] = {{MPV_RENDER_PARAM_OPENGL_FBO, &target},
                                 {MPV_RENDER_PARAM_FLIP_Y, &flip},
                                 {MPV_RENDER_PARAM_INVALID, nullptr}};
    mpv_render_context_render(render_, params);
    return resized;
}

}  // namespace canvas::scene

#else  // without libmpv

namespace canvas::scene {

bool VideoTexture::available() { return false; }

std::unique_ptr<VideoTexture> VideoTexture::open(std::span<const uint8_t>,
                                                 std::shared_ptr<const void>, std::string& error) {
    error = "video textures need a build with libmpv";
    return nullptr;
}

VideoTexture::~VideoTexture() = default;

void VideoTexture::onUpdate(void*) {}

bool VideoTexture::update() { return false; }

}  // namespace canvas::scene

#endif
