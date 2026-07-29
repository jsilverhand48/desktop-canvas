// Implementation of VideoInstance declared in VideoBackend.h.
#include "render/VideoBackend.h"

#include <EGL/egl.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <cstdint>

#include "core/EventLoop.h"
#include "core/Log.h"
#include "display/RenderSurface.h"

namespace canvas {

namespace {
void* getProcAddress(void*, const char* name) {
    return reinterpret_cast<void*>(eglGetProcAddress(name));
}
}  // namespace

VideoInstance::VideoInstance(EventLoop& loop, RenderSurface& surface,
                             std::string wallpaperId, std::string videoPath,
                             std::string scaling, bool muted,
                             int volumePercent)
    : loop_(loop),
      surface_(surface),
      wallpaper_id_(std::move(wallpaperId)),
      video_path_(std::move(videoPath)),
      scaling_(std::move(scaling)),
      muted_(muted),
      volume_(volumePercent) {}

VideoInstance::~VideoInstance() { stop(); }

const std::string& VideoInstance::outputName() const {
    return surface_.name();
}

void VideoInstance::onMpvWakeup(void* ctx) {
    // Called on an mpv internal thread; poke the loop thread.
    auto* self = static_cast<VideoInstance*>(ctx);
    uint64_t one = 1;
    (void)!write(self->wakeup_fd_, &one, sizeof(one));
}

bool VideoInstance::initMpv() {
    mpv_ = mpv_create();
    if (!mpv_) return false;
    mpv_set_option_string(mpv_, "vo", "libmpv");
    mpv_set_option_string(mpv_, "hwdec", "auto-safe");
    mpv_set_option_string(mpv_, "loop-file", "inf");
    mpv_set_option_string(mpv_, "video-timing-offset", "0");
    mpv_set_option_string(mpv_, "audio-client-name", "desktop-canvas");
    mpv_set_option_string(mpv_, "mute", muted_ ? "yes" : "no");
    mpv_set_option_string(mpv_, "volume", std::to_string(volume_).c_str());
    // Scaling: fill crops to cover, fit letterboxes, stretch ignores aspect.
    if (scaling_ == "stretch") {
        mpv_set_option_string(mpv_, "keepaspect", "no");
    } else if (scaling_ == "fill") {
        mpv_set_option_string(mpv_, "panscan", "1.0");
    }  // "fit" is mpv's default behavior
    if (mpv_initialize(mpv_) < 0) {
        log::error() << "mpv_initialize failed";
        return false;
    }

    mpv_opengl_init_params gl_params{getProcAddress, nullptr};
    int advanced = 1;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE,
         const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_params},
        {MPV_RENDER_PARAM_ADVANCED_CONTROL, &advanced},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    // The GL context must be current on the creating thread.
    if (!surface_.makeCurrent()) {
        log::error() << "makeCurrent failed for " << surface_.name();
        return false;
    }
    if (mpv_render_context_create(&render_ctx_, mpv_, params) < 0) {
        log::error() << "mpv_render_context_create failed";
        return false;
    }

    wakeup_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    loop_.addFd(wakeup_fd_, EPOLLIN, [this](uint32_t) { handleWakeup(); });
    mpv_render_context_set_update_callback(render_ctx_, onMpvWakeup, this);
    mpv_set_wakeup_callback(mpv_, onMpvWakeup, this);

    const char* cmd[] = {"loadfile", video_path_.c_str(), nullptr};
    mpv_command_async(mpv_, 0, cmd);
    return true;
}

void VideoInstance::destroyMpv() {
    if (wakeup_fd_ >= 0) {
        loop_.removeFd(wakeup_fd_);
        close(wakeup_fd_);
        wakeup_fd_ = -1;
    }
    if (render_ctx_) {
        mpv_render_context_free(render_ctx_);
        render_ctx_ = nullptr;
    }
    if (mpv_) {
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
    }
}

void VideoInstance::start() {
    if (running_) return;
    // Defer mpv setup until the surface is configured.
    surface_.setOnReady([this] {
        if (!mpv_ && !initMpv()) {
            log::error() << "video init failed for " << wallpaper_id_;
            return;
        }
        surface_ready_ = true;
        maybeRender();
    });
    running_ = true;
    if (surface_.ready() && !mpv_) {
        if (initMpv()) {
            surface_ready_ = true;
        }
    }
}

void VideoInstance::stop() {
    if (!running_) return;
    running_ = false;
    surface_.setOnReady(nullptr);
    destroyMpv();
}

void VideoInstance::pause() {
    if (mpv_) mpv_set_option_string(mpv_, "pause", "yes");
}

void VideoInstance::resume() {
    if (mpv_) mpv_set_option_string(mpv_, "pause", "no");
}

void VideoInstance::setMuted(bool muted) {
    muted_ = muted;
    if (mpv_) mpv_set_option_string(mpv_, "mute", muted ? "yes" : "no");
}

void VideoInstance::handleWakeup() {
    uint64_t count;
    while (read(wakeup_fd_, &count, sizeof(count)) == sizeof(count)) {
    }
    if (!running_ || !render_ctx_) return;

    // Drain core events (errors, EOF; loop-file handles looping itself).
    while (mpv_) {
        mpv_event* ev = mpv_wait_event(mpv_, 0);
        if (ev->event_id == MPV_EVENT_NONE) break;
        if (ev->event_id == MPV_EVENT_LOG_MESSAGE) continue;
        if (ev->event_id == MPV_EVENT_END_FILE) {
            auto* end = static_cast<mpv_event_end_file*>(ev->data);
            if (end->reason == MPV_END_FILE_REASON_ERROR)
                log::error() << "mpv error on " << wallpaper_id_ << ": "
                             << mpv_error_string(end->error);
        }
    }

    uint64_t flags = mpv_render_context_update(render_ctx_);
    if (flags & MPV_RENDER_UPDATE_FRAME) {
        frame_pending_ = true;
        maybeRender();
    }
}

void VideoInstance::maybeRender() {
    if (!running_ || !frame_pending_ || !surface_ready_ || !surface_.ready())
        return;
    if (!surface_.makeCurrent()) return;

    mpv_opengl_fbo fbo{0, surface_.pixelWidth(), surface_.pixelHeight(), 0};
    int flip_y = 1;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    mpv_render_context_render(render_ctx_, params);
    frame_pending_ = false;
    surface_ready_ = false;
    surface_.swapBuffers();
    // First frame and every 600th thereafter, so debug logs show the
    // pipeline is alive without flooding.
    if (frames_rendered_++ % 600 == 0)
        log::debug() << surface_.name() << ": frame " << frames_rendered_
                     << " presented";
    surface_.requestFrame([this] {
        surface_ready_ = true;
        maybeRender();
    });
    mpv_render_context_report_swap(render_ctx_);
}

}  // namespace canvas
