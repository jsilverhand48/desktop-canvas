// Implementation of PulseCapture declared in PulseCapture.h.
#include "audio/PulseCapture.h"

#include <pulse/pulseaudio.h>

#include "audio/SpectrumAnalyzer.h"

namespace canvas {

PulseCapture::~PulseCapture() { stop(); }

bool PulseCapture::start(Frames frames, std::function<void(const std::string&)> failure,
                         std::string& error) {
    stop();
    frames_ = std::move(frames);
    failure_ = std::move(failure);
    failed_ = false;
    loop_ = pa_threaded_mainloop_new();
    if (!loop_) {
        error = "pa_threaded_mainloop_new failed";
        return false;
    }
    pa_proplist* props = pa_proplist_new();
    pa_proplist_sets(props, PA_PROP_APPLICATION_NAME, "desktop-canvas");
    pa_proplist_sets(props, PA_PROP_APPLICATION_ID, "io.github.jsilverhand48.DesktopCanvas");
    context_ = pa_context_new_with_proplist(pa_threaded_mainloop_get_api(loop_), "desktop-canvas", props);
    pa_proplist_free(props);
    if (!context_) {
        error = "pa_context_new failed";
        stop();
        return false;
    }
    pa_context_set_state_callback(context_, &PulseCapture::contextState, this);
    if (pa_context_connect(context_, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0) {
        error = std::string("cannot reach the sound server: ") + pa_strerror(pa_context_errno(context_));
        stop();
        return false;
    }
    if (pa_threaded_mainloop_start(loop_) < 0) {
        error = "pa_threaded_mainloop_start failed";
        stop();
        return false;
    }
    return true;
}

void PulseCapture::stop() {
    if (!loop_) return;
    pa_threaded_mainloop_lock(loop_);
    if (stream_) {
        pa_stream_set_state_callback(stream_, nullptr, nullptr);
        pa_stream_set_read_callback(stream_, nullptr, nullptr);
        pa_stream_disconnect(stream_);
        pa_stream_unref(stream_);
        stream_ = nullptr;
    }
    if (context_) {
        pa_context_set_state_callback(context_, nullptr, nullptr);
        pa_context_disconnect(context_);
        pa_context_unref(context_);
        context_ = nullptr;
    }
    pa_threaded_mainloop_unlock(loop_);
    pa_threaded_mainloop_stop(loop_);
    pa_threaded_mainloop_free(loop_);
    loop_ = nullptr;
}

void PulseCapture::fail(const std::string& what) {
    if (failed_) return;
    failed_ = true;
    if (failure_) failure_(what);
}

void PulseCapture::contextState(pa_context* c, void* self) {
    auto* me = static_cast<PulseCapture*>(self);
    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_READY: {
            const pa_sample_spec spec{PA_SAMPLE_FLOAT32LE, uint32_t(SpectrumAnalyzer::kRate), 2};
            me->stream_ = pa_stream_new(c, "audio spectrum", &spec, nullptr);
            if (!me->stream_) {
                me->fail("cannot create a record stream");
                return;
            }
            pa_stream_set_state_callback(me->stream_, &PulseCapture::streamState, me);
            pa_stream_set_read_callback(me->stream_, &PulseCapture::streamRead, me);
            pa_buffer_attr attr{};
            attr.maxlength = uint32_t(-1);
            attr.fragsize = uint32_t(SpectrumAnalyzer::kRate / 100 * 2 * sizeof(float));  // 10 ms
            if (pa_stream_connect_record(me->stream_, "@DEFAULT_MONITOR@", &attr, PA_STREAM_ADJUST_LATENCY) < 0)
                me->fail(std::string("cannot record the output monitor: ") + pa_strerror(pa_context_errno(c)));
            break;
        }
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            me->fail(std::string("sound server: ") + pa_strerror(pa_context_errno(c)));
            break;
        default:
            break;
    }
}

void PulseCapture::streamState(pa_stream* s, void* self) {
    auto* me = static_cast<PulseCapture*>(self);
    const pa_stream_state_t state = pa_stream_get_state(s);
    if (state == PA_STREAM_FAILED || state == PA_STREAM_TERMINATED)
        me->fail(std::string("record stream ended: ") + pa_strerror(pa_context_errno(pa_stream_get_context(s))));
}

void PulseCapture::streamRead(pa_stream* s, size_t /*bytes*/, void* self) {
    auto* me = static_cast<PulseCapture*>(self);
    while (pa_stream_readable_size(s) > 0) {
        const void* data = nullptr;
        size_t n = 0;
        if (pa_stream_peek(s, &data, &n) < 0) return;
        if (n == 0) return;
        // A hole (data null) is silence the server dropped: skip it.
        if (data && me->frames_) me->frames_(static_cast<const float*>(data), n / (2 * sizeof(float)));
        pa_stream_drop(s);
    }
}

}  // namespace canvas
