// Watches the sound server for other applications playing, per
// src/scene/audio/README.md section 6.
//
// othersPlaying() is the rule: some playback stream that is not corked
// and whose process is neither the daemon nor one of its descendants
// (helpers and their sound players, web helpers, lwe). PlaybackWatcher
// applies it to the server's sink inputs over the PulseAudio protocol
// (PipeWire serves it too), re-checking on every sink input event from
// libpulse's own thread, and reports changes on the event loop: true at
// once, false only after kHoldMs without other playback. A lost server is
// retried every kRetrySeconds. Built only with libpulse (CANVAS_HAVE_PULSE).
#pragma once

#include <sys/types.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct pa_threaded_mainloop;
struct pa_context;

namespace canvas {

class EventLoop;

struct PlaybackStream {
    pid_t pid = 0;  // 0 when the stream names no process
    bool corked = false;
};

// parentOf(pid) is pid's parent, 0 when unknown.
bool othersPlaying(const std::vector<PlaybackStream>& streams, pid_t self,
                   const std::function<pid_t(pid_t)>& parentOf);
// The parent of pid from /proc; 0 when unreadable.
pid_t procParent(pid_t pid);

class PlaybackWatcher {
public:
    static constexpr int kHoldMs = 1000;
    static constexpr int kRetrySeconds = 10;

    PlaybackWatcher(EventLoop& loop, std::function<void(bool)> onChange);
    ~PlaybackWatcher();
    PlaybackWatcher(const PlaybackWatcher&) = delete;
    PlaybackWatcher& operator=(const PlaybackWatcher&) = delete;

    void start();
    void stop();
    bool playing() const { return reported_; }

private:
    void connect();
    void disconnect();
    void verdict(bool playing);  // loop thread
    void failed();               // loop thread

    static void contextState(pa_context* c, void* self);
    static void subscribed(pa_context* c, int type, uint32_t index, void* self);
    void query();  // pulse thread

    EventLoop& loop_;
    std::function<void(bool)> onChange_;
    pa_threaded_mainloop* mainloop_ = nullptr;
    pa_context* context_ = nullptr;
    std::vector<PlaybackStream> collecting_;  // pulse thread
    bool running_ = false;
    bool reported_ = false;
    uint64_t holdTimer_ = 0;
    uint64_t retryTimer_ = 0;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace canvas
