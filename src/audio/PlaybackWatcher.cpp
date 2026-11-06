// Implementation of PlaybackWatcher declared in PlaybackWatcher.h.
#include "audio/PlaybackWatcher.h"

#include <pulse/pulseaudio.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "core/EventLoop.h"
#include "core/Log.h"

namespace canvas {

pid_t procParent(pid_t pid) {
    std::ifstream in("/proc/" + std::to_string(pid) + "/stat");
    std::string line;
    if (!std::getline(in, line)) return 0;
    // "pid (comm) state ppid ...": comm may hold spaces and parentheses.
    const size_t close = line.rfind(')');
    if (close == std::string::npos) return 0;
    std::istringstream rest(line.substr(close + 1));
    std::string state;
    long ppid = 0;
    rest >> state >> ppid;
    return pid_t(ppid);
}

bool othersPlaying(const std::vector<PlaybackStream>& streams, pid_t self,
                   const std::function<pid_t(pid_t)>& parentOf) {
    for (const auto& s : streams) {
        if (s.corked) continue;
        bool ours = false;
        pid_t p = s.pid;
        for (int depth = 0; p > 1 && depth < 64; depth++) {
            if (p == self) {
                ours = true;
                break;
            }
            p = parentOf(p);
        }
        if (!ours) return true;
    }
    return false;
}

PlaybackWatcher::PlaybackWatcher(EventLoop& loop, std::function<void(bool)> onChange)
    : loop_(loop), onChange_(std::move(onChange)) {}

PlaybackWatcher::~PlaybackWatcher() {
    *alive_ = false;
    stop();
}

void PlaybackWatcher::start() {
    if (running_) return;
    running_ = true;
    connect();
}

void PlaybackWatcher::stop() {
    running_ = false;
    if (holdTimer_) loop_.cancelTimer(holdTimer_);
    if (retryTimer_) loop_.cancelTimer(retryTimer_);
    holdTimer_ = retryTimer_ = 0;
    disconnect();
    if (reported_) {
        reported_ = false;
        if (onChange_) onChange_(false);
    }
}

void PlaybackWatcher::connect() {
    mainloop_ = pa_threaded_mainloop_new();
    if (!mainloop_) return failed();
    pa_proplist* props = pa_proplist_new();
    pa_proplist_sets(props, PA_PROP_APPLICATION_NAME, "desktop-canvas");
    context_ = pa_context_new_with_proplist(pa_threaded_mainloop_get_api(mainloop_), "desktop-canvas", props);
    pa_proplist_free(props);
    if (!context_) return failed();
    pa_context_set_state_callback(context_, &PlaybackWatcher::contextState, this);
    pa_context_set_subscribe_callback(
        context_,
        [](pa_context* c, pa_subscription_event_type_t t, uint32_t i, void* self) {
            subscribed(c, int(t), i, self);
        },
        this);
    if (pa_context_connect(context_, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0 ||
        pa_threaded_mainloop_start(mainloop_) < 0)
        return failed();
}

void PlaybackWatcher::disconnect() {
    if (!mainloop_) return;
    pa_threaded_mainloop_lock(mainloop_);
    if (context_) {
        pa_context_set_state_callback(context_, nullptr, nullptr);
        pa_context_set_subscribe_callback(context_, nullptr, nullptr);
        pa_context_disconnect(context_);
        pa_context_unref(context_);
        context_ = nullptr;
    }
    pa_threaded_mainloop_unlock(mainloop_);
    pa_threaded_mainloop_stop(mainloop_);
    pa_threaded_mainloop_free(mainloop_);
    mainloop_ = nullptr;
}

void PlaybackWatcher::contextState(pa_context* c, void* self) {
    auto* me = static_cast<PlaybackWatcher*>(self);
    std::weak_ptr<bool> alive = me->alive_;
    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_READY:
            pa_operation_unref(pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SINK_INPUT, nullptr, nullptr));
            me->query();
            break;
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            me->loop_.post([me, alive] {
                if (auto a = alive.lock(); a && *a) me->failed();
            });
            break;
        default:
            break;
    }
}

void PlaybackWatcher::subscribed(pa_context*, int type, uint32_t, void* self) {
    if ((type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SINK_INPUT)
        static_cast<PlaybackWatcher*>(self)->query();
}

void PlaybackWatcher::query() {
    collecting_.clear();
    auto info = [](pa_context*, const pa_sink_input_info* i, int eol, void* self) {
        auto* me = static_cast<PlaybackWatcher*>(self);
        if (eol) {
            const bool playing = othersPlaying(me->collecting_, getpid(), procParent);
            std::weak_ptr<bool> alive = me->alive_;
            me->loop_.post([me, alive, playing] {
                if (auto a = alive.lock(); a && *a) me->verdict(playing);
            });
            return;
        }
        PlaybackStream s;
        s.corked = i->corked != 0;
        if (const char* pid = pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_PROCESS_ID))
            s.pid = pid_t(std::atol(pid));
        me->collecting_.push_back(s);
    };
    if (pa_operation* op = pa_context_get_sink_input_info_list(context_, info, this)) pa_operation_unref(op);
}

void PlaybackWatcher::verdict(bool playing) {
    if (!running_) return;
    if (playing) {
        if (holdTimer_) loop_.cancelTimer(holdTimer_);
        holdTimer_ = 0;
        if (!reported_) {
            reported_ = true;
            if (onChange_) onChange_(true);
        }
        return;
    }
    if (!reported_ || holdTimer_) return;
    holdTimer_ = loop_.addTimer(kHoldMs, [this] {
        holdTimer_ = 0;
        reported_ = false;
        if (onChange_) onChange_(false);
    });
}

void PlaybackWatcher::failed() {
    if (!running_) return;
    log::warn() << "sound server not reachable for automute; retrying in " << kRetrySeconds << " s";
    disconnect();
    if (retryTimer_) return;
    retryTimer_ = loop_.addTimer(uint64_t(kRetrySeconds) * 1000, [this] {
        retryTimer_ = 0;
        if (running_) connect();
    });
}

}  // namespace canvas
