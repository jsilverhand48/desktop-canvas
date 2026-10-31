// Video textures (src/scene/render/README.md section 9): an MP4 held in
// memory played in a loop, without sound, into a GL texture through
// libmpv's render API.
//
// open() copies the bytes into a memfd and has mpv load "fd://N", so
// nothing touches the disk; mpv decodes on its own threads (software
// decoding, no config files, no terminal). update() renders mpv's newest
// frame into the texture when there is one (call it once per frame with
// the GL context current); texture() is 0 until the first frame. Built
// only with libmpv (CANVAS_SCENE_HAVE_MPV); otherwise open() reports that
// video textures need it.
#pragma once

#include <epoxy/gl.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

struct mpv_handle;
struct mpv_render_context;

namespace canvas::scene {

class VideoTexture {
public:
    static std::unique_ptr<VideoTexture> open(std::span<const uint8_t> mp4, std::string& error);
    // True when this build plays video textures (libmpv).
    static bool available();
    ~VideoTexture();
    VideoTexture(const VideoTexture&) = delete;
    VideoTexture& operator=(const VideoTexture&) = delete;

    // Renders the newest frame, if mpv has one; true when the texture
    // changed size (callers update what they derived from it).
    bool update();
    GLuint texture() const { return texture_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    VideoTexture() = default;
    static void onUpdate(void* self);

    mpv_handle* mpv_ = nullptr;
    mpv_render_context* render_ = nullptr;
    int memfd_ = -1;
    GLuint fbo_ = 0, texture_ = 0;
    int width_ = 0, height_ = 0;
    std::atomic<bool> pending_{false};
};

}  // namespace canvas::scene
