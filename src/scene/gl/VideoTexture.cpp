// Implementation of VideoTexture declared in VideoTexture.h.
#include "scene/gl/VideoTexture.h"

#ifdef CANVAS_SCENE_HAVE_MPV

#include <epoxy/egl.h>
#include <mpv/client.h>
#include <mpv/render_gl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "core/Log.h"

namespace canvas::scene {

namespace {

void* procAddress(void*, const char* name) {
    return reinterpret_cast<void*>(eglGetProcAddress(name));
}

bool writeAll(int fd, std::span<const uint8_t> bytes) {
    size_t done = 0;
    while (done < bytes.size()) {
        ssize_t n = ::write(fd, bytes.data() + done, bytes.size() - done);
        if (n <= 0) return false;
        done += size_t(n);
    }
    return true;
}

}  // namespace

bool VideoTexture::available() { return true; }

std::unique_ptr<VideoTexture> VideoTexture::open(std::span<const uint8_t> mp4, std::string& error) {
    std::unique_ptr<VideoTexture> v(new VideoTexture());
    v->memfd_ = memfd_create("desktop-canvas-video", MFD_CLOEXEC);
    if (v->memfd_ < 0 || !writeAll(v->memfd_, mp4) || lseek(v->memfd_, 0, SEEK_SET) != 0) {
        error = "cannot hold the video in memory";
        return nullptr;
    }
    v->mpv_ = mpv_create();
    if (!v->mpv_) {
        error = "mpv_create failed";
        return nullptr;
    }
    for (auto [name, value] : {std::pair{"config", "no"}, {"terminal", "no"}, {"msg-level", "all=error"},
                               {"vo", "libmpv"}, {"audio", "no"}, {"aid", "no"}, {"sid", "no"},
                               {"loop-file", "inf"}, {"hwdec", "no"}, {"cache", "no"},
                               {"input-default-bindings", "no"}, {"osc", "no"}})
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
    const std::string url = "fd://" + std::to_string(v->memfd_);
    const char* cmd[] = {"loadfile", url.c_str(), nullptr};
    if (mpv_command(v->mpv_, cmd) < 0) {
        error = "mpv cannot load the video";
        return nullptr;
    }
    return v;
}

VideoTexture::~VideoTexture() {
    if (render_) mpv_render_context_free(render_);  // before the handle
    if (mpv_) mpv_terminate_destroy(mpv_);
    if (memfd_ >= 0) ::close(memfd_);
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

std::unique_ptr<VideoTexture> VideoTexture::open(std::span<const uint8_t>, std::string& error) {
    error = "video textures need a build with libmpv";
    return nullptr;
}

VideoTexture::~VideoTexture() = default;

void VideoTexture::onUpdate(void*) {}

bool VideoTexture::update() { return false; }

}  // namespace canvas::scene

#endif
