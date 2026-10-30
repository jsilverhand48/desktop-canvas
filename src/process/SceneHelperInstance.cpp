// Implementation of SceneHelperInstance declared in SceneHelperInstance.h.
#include "process/SceneHelperInstance.h"

#include <signal.h>

#include "core/ControlChannel.h"
#include "core/EventLoop.h"
#include "core/Log.h"
#include "process/HelperProtocol.h"

namespace canvas {

SceneHelperInstance::SceneHelperInstance(EventLoop& loop,
                                         ChildSupervisor& supervisor,
                                         std::string tag,
                                         std::string wallpaperId, bool muted,
                                         int volume, Launch launch)
    : loop_(loop),
      supervisor_(supervisor),
      tag_(std::move(tag)),
      output_name_(launch.outputs.empty() ? std::string()
                                          : launch.outputs.front()),
      wallpaper_id_(std::move(wallpaperId)),
      muted_(muted),
      volume_(volume),
      launch_(std::move(launch)) {}

SceneHelperInstance::~SceneHelperInstance() {
    *alive_ = false;
    stop();
}

std::vector<std::string> SceneHelperInstance::buildArgv(const Launch& launch,
                                                        bool muted,
                                                        int volume) {
    std::vector<std::string> argv{
        launch.helperPath, "--control-fd", std::to_string(helper::kControlFd)};
    for (const auto& output : launch.outputs) {
        argv.push_back("--output");
        argv.push_back(output);
    }
    argv.insert(argv.end(), {"--layer", launch.layer, "--fps",
                             std::to_string(launch.fps), "--scaling",
                             launch.scaling});
    if (!launch.assetsDir.empty()) {
        argv.push_back("--assets-dir");
        argv.push_back(launch.assetsDir);
    }
    for (const auto& [name, value] : launch.properties) {
        argv.push_back("--set-property");
        argv.push_back(name + "=" + value);
    }
    if (muted) argv.push_back("--muted");
    argv.push_back("--volume");
    argv.push_back(std::to_string(volume));
    if (launch.strict) argv.push_back("--strict");
    argv.push_back(launch.wallpaperDir);
    return argv;
}

void SceneHelperInstance::start() {
    if (started_) return;
    started_ = true;
    ChildSpec spec;
    spec.argv = buildArgv(launch_, muted_, volume_);
    spec.exitPolicy = ExitPolicy::HelperProtocol;
    spec.controlChannel = true;
    spec.onSpawn = [this](pid_t pid, int fd) { onSpawn(pid, fd); };
    spec.onExit = [this](ExitKind kind, int) { onExit(kind); };
    supervisor_.start(tag_, std::move(spec));
}

void SceneHelperInstance::stop() {
    if (!started_) return;
    started_ = false;
    cancelTimers();
    // Closing our end first lets a helper that is still alive see EOF and
    // exit on its own; the supervisor's SIGTERM follows either way.
    channel_.reset();
    supervisor_.stop(tag_);
}

void SceneHelperInstance::cancelTimers() {
    if (pointer_timer_) loop_.cancelTimer(pointer_timer_);
    if (watchdog_timer_) loop_.cancelTimer(watchdog_timer_);
    pointer_timer_ = 0;
    watchdog_timer_ = 0;
}

void SceneHelperInstance::onSpawn(pid_t /*pid*/, int fd) {
    helper_ready_ = false;
    unsupported_.clear();
    channel_ = std::make_unique<ControlChannel>(loop_, fd);
    channel_->setOnMessage([this](std::string data) { handleMessage(data); });
    armWatchdog();
}

void SceneHelperInstance::onExit(ExitKind kind) {
    cancelTimers();
    channel_.reset();
    helper_ready_ = false;
    if (kind == ExitKind::Crash) return;  // the supervisor respawns it
    started_ = false;  // the supervisor has forgotten the tag
    if (kind != ExitKind::Unsupported) return;
    std::string list;
    for (const auto& feature : unsupported_)
        list += (list.empty() ? "" : ", ") + feature;
    log::info() << "native renderer cannot show " << wallpaper_id_
                << (list.empty() ? std::string() : " (needs " + list + ")");
    // Deferred: the owner typically replaces (destroys) this instance.
    std::weak_ptr<bool> alive = alive_;
    auto features = unsupported_;
    loop_.post([this, alive, features] {
        auto guard = alive.lock();
        if (!guard || !*guard) return;
        // Copy: the callback usually destroys this instance (and with it
        // on_unsupported_) while it runs. Nothing touches `this` after.
        auto cb = on_unsupported_;
        if (cb) cb(features);
    });
}

void SceneHelperInstance::handleMessage(const std::string& data) {
    armWatchdog();  // any message proves the helper is alive
    auto message = helper::decode(data);
    if (!message) {
        log::warn() << tag_ << ": malformed message from the helper";
        return;
    }
    const std::string type = helper::typeOf(*message);
    if (type == "hello") {
        int proto = message->value("proto", 0);
        if (proto != helper::kProtocolVersion)
            log::warn() << tag_ << ": helper speaks protocol " << proto
                        << ", expected " << helper::kProtocolVersion;
        sendState();
    } else if (type == "ready") {
        helper_ready_ = true;
        log::info() << tag_ << ": native renderer showing " << wallpaper_id_;
    } else if (type == "unsupported") {
        unsupported_.clear();
        auto features = message->find("features");
        if (features != message->end() && features->is_array())
            for (const auto& f : *features)
                if (f.is_string() && unsupported_.size() < 64)
                    unsupported_.push_back(f.get<std::string>().substr(0, 200));
    } else if (type == "error") {
        log::warn() << tag_ << ": helper error: "
                    << message->value("message", "").substr(0, 500);
    } else if (type == "stats") {
        log::debug() << tag_ << ": " << message->value("fps", 0.0) << " fps, "
                     << message->value("cpu_ms", 0.0) << " ms cpu";
    }
}

void SceneHelperInstance::send(const nlohmann::json& message) {
    if (channel_ && channel_->isOpen()) channel_->send(helper::encode(message));
}

void SceneHelperInstance::sendState() {
    send({{"type", "mute"}, {"muted", muted_}});
    send({{"type", "volume"}, {"volume", volume_}});
    if (paused_) send({{"type", "pause"}});
    flushPointer();
}

void SceneHelperInstance::pause() {
    paused_ = true;
    send({{"type", "pause"}});
}

void SceneHelperInstance::resume() {
    paused_ = false;
    send({{"type", "resume"}});
}

void SceneHelperInstance::setMuted(bool muted) {
    muted_ = muted;
    send({{"type", "mute"}, {"muted", muted}});
}

bool SceneHelperInstance::setVolume(int percent) {
    volume_ = percent;
    send({{"type", "volume"}, {"volume", percent}});
    return true;
}

void SceneHelperInstance::setPointer(const std::string& output, double x,
                                     double y) {
    pointer_[output] = {x, y};
    if (pointer_timer_ || !channel_) return;
    pointer_timer_ = loop_.addTimer(kPointerIntervalMs, [this] {
        pointer_timer_ = 0;
        flushPointer();
    });
}

void SceneHelperInstance::flushPointer() {
    for (const auto& [output, xy] : pointer_)
        send({{"type", "pointer"},
              {"output", output},
              {"x", xy.first},
              {"y", xy.second}});
    if (channel_) pointer_.clear();
}

void SceneHelperInstance::armWatchdog() {
    if (watchdog_timer_) loop_.cancelTimer(watchdog_timer_);
    watchdog_timer_ = loop_.addTimer(helper::kHeartbeatTimeoutMs, [this] {
        watchdog_timer_ = 0;
        log::warn() << tag_ << ": no heartbeat for "
                    << helper::kHeartbeatTimeoutMs / 1000
                    << "s, killing the hung helper";
        supervisor_.signalChild(tag_, SIGKILL);
    });
}

}  // namespace canvas
