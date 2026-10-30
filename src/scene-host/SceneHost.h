// Runs one scene on a set of outputs inside desktop-canvas-scene.
//
// SceneHost owns everything around the pixels: a RenderSurface per wanted
// output (created through the Platform, recreated when an output comes back
// or a surface reports closed()), frame pacing, and the daemon side of
// process/HelperProtocol.h. Pixels come from the Renderer.
//
// Pacing: a FrameClock ticks at the fps cap. Each tick advances the
// renderer once, then renders and presents to every output that is ready,
// not covered by a fullscreen window, and not still waiting for its
// previous frame callback (RenderSurface contract: swap, requestFrame, no
// second swap before the callback). The clock runs only while the host is
// not paused and at least one output could present, so a paused or fully
// covered scene costs no CPU or GPU time.
//
// Protocol: attachChannel() sends hello. "ready" goes out once, after every
// wanted output that exists has presented a frame. "stats" goes out every
// helper::kStatsIntervalMs and is the daemon's heartbeat. Daemon messages
// (pause, resume, quit, mute, volume, pointer, props, covered) are applied
// as they arrive; quit and a closed channel end the event loop with
// helper::kExitClean.
//
// No GL here: the host only calls RenderSurface::makeCurrent/swapBuffers
// and the Renderer, so tests drive it with fakes.
#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "scene-host/FrameClock.h"

namespace canvas {
class ControlChannel;
class EventLoop;
class Platform;
class RenderSurface;
}  // namespace canvas

namespace canvas::scenehost {

class Renderer;

struct SceneHostOptions {
    // Outputs to cover by connector name; empty means every output.
    std::vector<std::string> outputs;
    std::string layer = "background";
    int fps = 30;
    // One canvas stretched over all outputs (config "spans"). Until the
    // engine composes scenes (plan phase P4) each output still renders the
    // whole frame; the flag is carried so the daemon side is complete.
    bool span = false;
};

class SceneHost {
public:
    SceneHost(EventLoop& loop, Platform& platform, Renderer& renderer,
              SceneHostOptions options);
    ~SceneHost();
    SceneHost(const SceneHost&) = delete;
    SceneHost& operator=(const SceneHost&) = delete;

    // Takes the daemon channel and sends hello. Optional: a helper started
    // by hand runs without one.
    void attachChannel(std::unique_ptr<ControlChannel> channel);

    // Creates surfaces for the wanted outputs and starts ticking.
    void start();

    bool paused() const { return paused_; }
    bool readySent() const { return ready_sent_; }
    uint64_t ticks() const { return ticks_; }
    // Frames presented on one output so far (0 for unknown outputs).
    uint64_t framesOn(const std::string& output) const;
    bool clockRunning() const { return clock_.running(); }

private:
    struct Output {
        std::unique_ptr<RenderSurface> surface;
        bool framePending = false;  // swapped, frame callback not back yet
        bool covered = false;       // a fullscreen window hides it
        uint64_t frames = 0;
    };

    bool wanted(const std::string& name) const;
    // Creates or drops surfaces so outputs_ matches platform outputs that
    // are wanted; replaces surfaces that report closed().
    void syncOutputs();
    void tick();
    void present(const std::string& name, Output& output);
    void updateClock();
    void maybeSendReady();
    void handleMessage(const std::string& data);
    void send(const nlohmann::json& message);
    // Sends stats every helper::kStatsIntervalMs (the daemon's heartbeat).
    void scheduleStats();
    void sendStats();

    EventLoop& loop_;
    Platform& platform_;
    Renderer& renderer_;
    SceneHostOptions options_;
    FrameClock clock_;
    std::unique_ptr<ControlChannel> channel_;
    std::map<std::string, Output> outputs_;
    bool started_ = false;
    bool paused_ = false;
    bool ready_sent_ = false;
    uint64_t ticks_ = 0;
    std::chrono::steady_clock::time_point last_tick_{};
    // Stats window.
    uint64_t stats_timer_ = 0;
    uint64_t window_ticks_ = 0;
    std::chrono::nanoseconds window_cpu_{0};
    std::chrono::steady_clock::time_point window_start_{};
};

}  // namespace canvas::scenehost
