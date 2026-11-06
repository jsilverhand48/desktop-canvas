// WallpaperInstance for scenes rendered by desktop-canvas-scene, the
// project's own helper, driven over process/HelperProtocol.h.
//
// The child runs under ChildSupervisor with ExitPolicy::HelperProtocol and
// a control channel. Everything that lwe needed restarts or signals for is
// a live message here: pause and resume, mute, volume, pointer, coverage
// by fullscreen windows. Each new helper process (first start or a respawn
// after a crash) gets the current pause, mute, volume and coverage state
// re-sent after its hello, plus the latest pointer position.
//
// Pointer updates are coalesced: the latest position per output is kept and
// at most one burst is sent every kPointerIntervalMs. A helper that sends
// nothing (its stats double as a heartbeat) for helper::kHeartbeatTimeoutMs
// is considered hung and gets SIGKILL, which the supervisor counts as a
// crash.
//
// A strict helper (engine "auto") that finds unsupported content sends
// {"type":"unsupported"} and exits with kExitUnsupported; onUnsupported then
// fires (deferred to the next loop iteration, so the owner may destroy this
// instance from it) with the features it listed. A helper that exits
// cleanly or with a usage error is not restarted; the output keeps its last
// frame until the owner applies something else.
#pragma once

#include <sys/types.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "process/ChildSupervisor.h"
#include "render/WallpaperInstance.h"

namespace canvas {

class ControlChannel;
class EventLoop;

class SceneHelperInstance : public WallpaperInstance {
public:
    static constexpr int kPointerIntervalMs = 16;

    struct Launch {
        std::string helperPath;
        std::string wallpaperDir;
        std::vector<std::string> outputs;  // every output of the group
        std::string layer = "background";
        int fps = 30;
        std::string scaling = "fill";
        std::string assetsDir;  // empty = helper autodetects
        std::vector<std::pair<std::string, std::string>> properties;
        bool strict = false;  // engine "auto": exit 3 on unsupported content
        bool span = false;    // one canvas over all outputs (config spans)
        // The audio spectrum memory to hand the helper (--audio-fd); -1
        // none. Not owned; must outlive the instance.
        int audioFd = -1;
    };

    // tag = "native:<first output>" (InstanceManager::handleBroken uses the
    // part after ':' to find the slot).
    SceneHelperInstance(EventLoop& loop, ChildSupervisor& supervisor,
                        std::string tag, std::string wallpaperId, bool muted,
                        int volume, Launch launch);
    ~SceneHelperInstance() override;

    // The helper's command line for this launch state (no secrets, safe to
    // log); exposed for tests.
    static std::vector<std::string> buildArgv(const Launch& launch, bool muted,
                                              int volume);

    void start() override;
    void stop() override;
    void pause() override;
    void resume() override;
    void setMuted(bool muted) override;
    bool setVolume(int percent) override;  // always live
    void setPointer(const std::string& output, double x, double y) override;
    // Sent as "covered"; the helper stops presenting on covered outputs.
    void setCovered(const std::string& output, bool covered) override;

    const std::string& wallpaperId() const override { return wallpaper_id_; }
    const std::string& outputName() const override { return output_name_; }
    const std::string& tag() const { return tag_; }

    // Called (deferred) when a strict helper reported unsupported content.
    void setOnUnsupported(
        std::function<void(const std::vector<std::string>& features)> cb) {
        on_unsupported_ = std::move(cb);
    }
    // Whether this helper needs the audio capture: its scene reads the
    // spectrum ("audio" message) and it runs unpaused. Called on changes
    // only, false last (exit, stop, destruction).
    void setOnAudioDemand(std::function<void(bool)> cb) { on_audio_demand_ = std::move(cb); }
    // True once the current helper process reported "ready".
    bool helperReady() const { return helper_ready_; }

private:
    void onSpawn(pid_t pid, int fd);
    void onExit(ExitKind kind);
    void handleMessage(const std::string& data);
    void send(const nlohmann::json& message);
    void sendState();
    void flushPointer();
    void armWatchdog();
    void cancelTimers();
    void reportAudioDemand();

    EventLoop& loop_;
    ChildSupervisor& supervisor_;
    std::string tag_;
    std::string output_name_;
    std::string wallpaper_id_;
    bool muted_;
    int volume_;
    Launch launch_;
    bool started_ = false;
    bool paused_ = false;
    bool helper_ready_ = false;
    std::unique_ptr<ControlChannel> channel_;
    std::vector<std::string> unsupported_;  // from the latest helper
    std::map<std::string, std::pair<double, double>> pointer_;  // pending
    std::map<std::string, bool> covered_;  // re-sent after every hello
    uint64_t pointer_timer_ = 0;
    uint64_t watchdog_timer_ = 0;
    std::function<void(const std::vector<std::string>&)> on_unsupported_;
    std::function<void(bool)> on_audio_demand_;
    bool audio_wanted_ = false;  // the current helper's last "audio" message
    bool audio_demand_ = false;  // last value reported
    // Cleared in the destructor; deferred callbacks check it.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace canvas
