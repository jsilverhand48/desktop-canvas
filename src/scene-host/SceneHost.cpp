// Implementation of SceneHost declared in SceneHost.h.
#include "scene-host/SceneHost.h"

#include <unistd.h>

#include <algorithm>
#include <fstream>

#include "core/ControlChannel.h"
#include "core/EventLoop.h"
#include "core/Log.h"
#include "display/Platform.h"
#include "display/RenderSurface.h"
#include "process/HelperProtocol.h"
#include "scene-host/Renderer.h"

namespace canvas::scenehost {

namespace {

// Resident set size in bytes, from /proc/self/statm.
uint64_t residentBytes() {
    std::ifstream statm("/proc/self/statm");
    uint64_t pages = 0;
    uint64_t resident = 0;
    if (!(statm >> pages >> resident)) return 0;
    return resident * static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
}

}  // namespace

SceneHost::SceneHost(EventLoop& loop, Platform& platform, Renderer& renderer,
                     SceneHostOptions options)
    : loop_(loop),
      platform_(platform),
      renderer_(renderer),
      options_(std::move(options)),
      clock_(loop, options_.fps, [this] { tick(); }) {}

SceneHost::~SceneHost() {
    clock_.stop();
    if (stats_timer_) loop_.cancelTimer(stats_timer_);
    // Surfaces go before the channel; nothing below calls back into us.
    outputs_.clear();
}

void SceneHost::attachChannel(std::unique_ptr<ControlChannel> channel) {
    channel_ = std::move(channel);
    channel_->setOnMessage([this](std::string data) { handleMessage(data); });
    channel_->setOnClosed([this] {
        // The daemon went away (or closed us): nothing left to show for.
        log::info() << "control channel closed, exiting";
        loop_.quit(helper::kExitClean);
    });
    send({{"type", "hello"},
          {"proto", helper::kProtocolVersion},
          {"pid", static_cast<int>(getpid())}});
}

void SceneHost::start() {
    if (started_) return;
    started_ = true;
    platform_.setOnOutputsChanged([this] { syncOutputs(); });
    // Pointer input the helper sees itself (its own Wayland surfaces on
    // wlroots, polling on X11); on Plasma the daemon forwards it instead.
    platform_.setOnPointer([this](const std::string& output, double x, double y) {
        renderer_.setPointer(output, x, y);
    });
    platform_.setOnPointerButton(
        [this](const std::string& output, uint32_t button, bool pressed) {
            renderer_.setPointerButton(output, button, pressed);
        });
    if (options_.span)
        log::info() << "span over " << options_.outputs.size()
                    << " outputs (each still renders the whole frame)";
    syncOutputs();
    window_start_ = std::chrono::steady_clock::now();
    last_tick_ = window_start_;
    if (channel_) scheduleStats();
    updateClock();
}

void SceneHost::scheduleStats() {
    stats_timer_ = loop_.addTimer(helper::kStatsIntervalMs, [this] {
        stats_timer_ = 0;
        sendStats();
        scheduleStats();
    });
}

bool SceneHost::wanted(const std::string& name) const {
    return options_.outputs.empty() ||
           std::find(options_.outputs.begin(), options_.outputs.end(),
                     name) != options_.outputs.end();
}

void SceneHost::syncOutputs() {
    if (options_.windowWidth > 0 && options_.windowHeight > 0) {
        Output& output = outputs_["window"];
        if (!output.surface) {
            output.surface = platform_.createWindow(
                options_.windowWidth, options_.windowHeight, "desktop-canvas-scene",
                [this] { loop_.quit(helper::kExitClean); });
            if (!output.surface) {
                log::error() << "could not create a window";
                outputs_.erase("window");
                loop_.quit(1);
                return;
            }
            output.surface->setOnReady([this] { updateClock(); });
            output.surface->show();
        }
        updateClock();
        maybeSendReady();
        return;
    }
    auto present = platform_.outputs();
    // Drop outputs that disappeared or are no longer wanted.
    for (auto it = outputs_.begin(); it != outputs_.end();) {
        bool exists = std::any_of(present.begin(), present.end(),
                                  [&](const auto& o) { return o.name == it->first; });
        if (!exists || !wanted(it->first))
            it = outputs_.erase(it);
        else
            ++it;
    }
    for (const auto& desc : present) {
        if (!wanted(desc.name)) continue;
        Output& output = outputs_[desc.name];
        if (output.surface && !output.surface->closed()) continue;
        output.surface = platform_.createSurface(desc.name, options_.layer);
        output.framePending = false;
        if (!output.surface) {
            log::error() << "could not create a surface on " << desc.name;
            outputs_.erase(desc.name);
            continue;
        }
        log::info() << "showing on " << desc.name;
        output.surface->setOnReady([this] { updateClock(); });
        output.surface->show();
    }
    updateClock();
    maybeSendReady();
}

void SceneHost::updateClock() {
    if (!started_) return;
    bool anyVisible = std::any_of(outputs_.begin(), outputs_.end(),
                                  [](const auto& entry) {
                                      return !entry.second.covered;
                                  });
    bool run = !paused_ && anyVisible;
    renderer_.setPaused(!run);
    if (run && !clock_.running()) {
        last_tick_ = std::chrono::steady_clock::now();
        clock_.start();
    } else if (!run && clock_.running()) {
        clock_.stop();
    }
}

void SceneHost::tick() {
    auto begin = std::chrono::steady_clock::now();
    // Simulated time follows the wall clock but never jumps by more than a
    // tenth of a second (after a pause, a stall or a suspend).
    double dt = std::chrono::duration<double>(begin - last_tick_).count();
    last_tick_ = begin;
    renderer_.advance(std::min(dt, 0.1));
    ticks_++;

    for (auto& [name, output] : outputs_) present(name, output);
    // The daemon captures audio only for helpers that read it.
    if (const bool wanted = renderer_.wantsAudio(); wanted != audio_wanted_) {
        audio_wanted_ = wanted;
        send({{"type", "audio"}, {"wanted", wanted}});
    }

    window_ticks_++;
    window_cpu_ += std::chrono::steady_clock::now() - begin;
    maybeSendReady();
}

void SceneHost::present(const std::string& name, Output& output) {
    RenderSurface* surface = output.surface.get();
    if (!surface || output.covered || output.framePending ||
        !surface->ready())
        return;
    if (!surface->makeCurrent()) {
        log::warn() << "makeCurrent failed on " << name;
        return;
    }
    renderer_.render(surface->pixelWidth(), surface->pixelHeight());
    surface->swapBuffers();
    output.frames++;
    output.framePending = true;
    // The callback may arrive after the output was dropped; look it up.
    surface->requestFrame([this, name] {
        auto it = outputs_.find(name);
        if (it != outputs_.end()) it->second.framePending = false;
    });
}

void SceneHost::maybeSendReady() {
    if (ready_sent_ || outputs_.empty()) return;
    bool all = std::all_of(outputs_.begin(), outputs_.end(),
                           [](const auto& entry) {
                               return entry.second.frames > 0;
                           });
    if (!all) return;
    ready_sent_ = true;
    log::info() << "first frame presented on every output";
    send({{"type", "ready"}});
}

uint64_t SceneHost::framesOn(const std::string& output) const {
    auto it = outputs_.find(output);
    return it == outputs_.end() ? 0 : it->second.frames;
}

void SceneHost::handleMessage(const std::string& data) {
    auto message = helper::decode(data);
    if (!message) {
        log::warn() << "ignoring malformed control message";
        return;
    }
    const std::string type = helper::typeOf(*message);
    if (type == "pause") {
        paused_ = true;
        updateClock();
    } else if (type == "resume") {
        paused_ = false;
        updateClock();
    } else if (type == "quit") {
        loop_.quit(helper::kExitClean);
    } else if (type == "mute") {
        renderer_.setMuted(message->value("muted", true));
    } else if (type == "volume") {
        renderer_.setVolume(std::clamp(message->value("volume", 0), 0, 100));
    } else if (type == "pointer") {
        renderer_.setPointer(message->value("output", ""),
                             message->value("x", 0.0),
                             message->value("y", 0.0));
    } else if (type == "media") {
        renderer_.setMedia(*message);
    } else if (type == "props") {
        auto values = message->find("values");
        if (values != message->end() && values->is_object()) {
            for (const auto& [name, value] : values->items()) {
                if (value.is_string())
                    renderer_.setProperty(name, value.get<std::string>());
            }
        }
    } else if (type == "covered") {
        auto it = outputs_.find(message->value("output", ""));
        if (it != outputs_.end()) {
            it->second.covered = message->value("covered", false);
            updateClock();
        }
    } else {
        log::debug() << "ignoring control message of type " << type;
    }
}

void SceneHost::send(const nlohmann::json& message) {
    if (channel_ && channel_->isOpen()) channel_->send(helper::encode(message));
}

void SceneHost::sendStats() {
    auto now = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(now - window_start_).count();
    double fps = seconds > 0 ? static_cast<double>(window_ticks_) / seconds : 0;
    double cpuMs =
        window_ticks_
            ? std::chrono::duration<double, std::milli>(window_cpu_).count() /
                  static_cast<double>(window_ticks_)
            : 0;
    send({{"type", "stats"},
          {"fps", fps},
          {"cpu_ms", cpuMs},
          {"gpu_ms", 0.0},
          {"rss", residentBytes()}});
    window_start_ = now;
    window_ticks_ = 0;
    window_cpu_ = std::chrono::nanoseconds(0);
}

}  // namespace canvas::scenehost
