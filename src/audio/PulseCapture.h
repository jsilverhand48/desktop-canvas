// AudioCaptureBackend over the PulseAudio protocol (served by PulseAudio
// and by PipeWire's pulse server), per src/scene/audio/README.md section 2:
// a record stream on @DEFAULT_MONITOR@ (what the default output plays),
// 48 kHz stereo float, 10 ms fragments, on libpulse's threaded mainloop.
// start() only begins connecting; a server that is missing, refuses or
// goes away later is reported through the failure callback. Built only
// with libpulse (CANVAS_HAVE_PULSE).
#pragma once

#include <string>

#include "audio/SpectrumService.h"

struct pa_threaded_mainloop;
struct pa_context;
struct pa_stream;

namespace canvas {

class PulseCapture : public AudioCaptureBackend {
public:
    PulseCapture() = default;
    ~PulseCapture() override;
    bool start(Frames frames, std::function<void(const std::string&)> failure, std::string& error) override;
    void stop() override;

private:
    static void contextState(pa_context* c, void* self);
    static void streamState(pa_stream* s, void* self);
    static void streamRead(pa_stream* s, size_t bytes, void* self);
    void fail(const std::string& what);  // on the mainloop thread

    pa_threaded_mainloop* loop_ = nullptr;
    pa_context* context_ = nullptr;
    pa_stream* stream_ = nullptr;
    Frames frames_;
    std::function<void(const std::string&)> failure_;
    bool failed_ = false;
};

}  // namespace canvas
