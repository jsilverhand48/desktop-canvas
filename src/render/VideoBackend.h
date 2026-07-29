// Video wallpaper rendering with libmpv's render API.
//
// One VideoInstance per (output, video wallpaper). The mpv core decodes and
// times the video (loop-file=inf); its update callback fires on an mpv
// thread and is bridged to the daemon's event loop with an eventfd. On the
// loop thread, when mpv reports a new frame AND the compositor has signaled
// readiness through the RenderSurface frame callback, the frame is rendered
// into the surface's default framebuffer and swapped. This double gating
// keeps the daemon from blocking on occluded surfaces and from rendering
// faster than the compositor can display.
//
// Audio: mpv option "mute" is set from AudioPolicy; the requirement is that
// wallpapers default to muted. Mute can be flipped live (no restart).
#pragma once

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <string>

#include "render/WallpaperInstance.h"

namespace canvas {

class EventLoop;
class RenderSurface;

class VideoInstance : public WallpaperInstance {
public:
    VideoInstance(EventLoop& loop, RenderSurface& surface,
                  std::string wallpaperId, std::string videoPath,
                  std::string scaling, bool muted, int volumePercent);
    ~VideoInstance() override;

    void start() override;
    void stop() override;
    void pause() override;
    void resume() override;
    void setMuted(bool muted) override;

    const std::string& wallpaperId() const override { return wallpaper_id_; }
    const std::string& outputName() const override;

private:
    static void onMpvWakeup(void* ctx);   // mpv thread -> eventfd
    void handleWakeup();                  // loop thread
    void maybeRender();
    bool initMpv();
    void destroyMpv();

    EventLoop& loop_;
    RenderSurface& surface_;
    std::string wallpaper_id_;
    std::string video_path_;
    std::string scaling_;
    bool muted_;
    int volume_;

    mpv_handle* mpv_ = nullptr;
    mpv_render_context* render_ctx_ = nullptr;
    int wakeup_fd_ = -1;
    bool frame_pending_ = false;   // mpv has a new frame for us
    bool surface_ready_ = false;   // compositor ready for the next swap
    bool running_ = false;
    uint64_t frames_rendered_ = 0;  // presented frame count (debug stat)
};

}  // namespace canvas
