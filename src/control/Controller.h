// Every daemon action and setting, in one place.
//
// The IPC socket (desktop-canvasctl) and the browser settings UI (WebApi)
// both go through Controller, so each operation has exactly one
// implementation. Mutations update Config, persist it to
// config.sourcePath, and trigger the live effect the change needs:
//
//   fps, scaling, layer, scene engine (lwe | native | auto),
//   engine path, assets, mouse, web.enabled
//                                    -> InstanceManager::restartAll()
//   content_roots                    -> rescan, then restartAll()
//   audio volume                     -> InstanceManager::setVolume() (live;
//                                       restarts only unmuted lwe/web)
//   audio muted                      -> InstanceManager::setMuted()
//   a wallpaper's engine, retry_native
//                                    -> restartWallpaper(id) (retry_native
//                                       first forgets the native verdict)
//   fullscreen_pause rules           -> InstanceManager::refreshCoverage()
//   spans                            -> restartAll()
//   kde_bridge                       -> next daemon start (reported back)
//   assignments                      -> InstanceManager::setAssignment()
//   broken (ids removed)             -> InstanceManager::clearBroken()
//   enabled wallpapers               -> Rotator::refresh()
//   playlist interval / outputs      -> Rotator::refresh(rearm) or stop()
//   scene randomize / nsfw, pinned or allowed values
//                                    -> next launch (optionally relaunch
//                                       now via "apply": true)
//   ui port / enabled                -> next daemon start (reported back)
//
// JSON shapes are documented in http/WebApi.h (HTTP) and ipc/Protocol.h
// (IPC). Errors are reported as false plus a message; nothing throws.
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace canvas {

class AudioPolicy;
class EventLoop;
class InstanceManager;
class Platform;
class Rotator;
struct Config;

class Controller {
public:
    using json = nlohmann::json;

    Controller(EventLoop& loop, Platform* platform, InstanceManager& manager,
               Rotator& rotator, AudioPolicy& audio, Config& config);

    // Outputs and what they show, rotation state, mute/pause, UI address,
    // library_revision (changes on every rescan, e.g. folders added to the
    // Steam library) and library_size.
    json status() const;
    // Every library item with its saved per wallpaper state.
    json library() const;
    // Every global setting (the config minus "wallpapers").
    json settings() const;
    // Applies a partial settings object (same shape as settings()).
    // On success `notes` may say what needs a daemon restart.
    bool updateSettings(const json& patch, std::string& error,
                        std::vector<std::string>& notes);

    // One wallpaper: library fields, its user properties from project.json,
    // and the saved pinned / allowed values.
    bool wallpaper(const std::string& id, json& out, std::string& error) const;
    // Patch: {"enabled", "properties", "allowed", "apply"}; the maps
    // replace the saved ones wholesale.
    bool updateWallpaper(const std::string& id, const json& patch,
                         std::string& error);
    // Bulk enable / disable for the rotation pool.
    bool setEnabled(const std::vector<std::string>& ids, bool enabled,
                    std::string& error);
    // Shows id now on output ("" or "*" = every output) without changing
    // config; the next rotation tick takes the outputs back.
    bool show(const std::string& id, const std::string& output,
              std::string& error);
    // next | pause | resume | mute | unmute | reload | restart
    bool action(const std::string& name, std::string& error);

    // Full IPC request line -> reply line (see ipc/Protocol.h).
    std::string handleIpc(const std::string& line);

    // Address of the settings UI, "" when it is not serving.
    void setUiUrl(std::string url) { uiUrl_ = std::move(url); }

private:
    bool save(std::string& error);

    EventLoop& loop_;
    Platform* platform_;  // null in tests / --list mode
    InstanceManager& manager_;
    Rotator& rotator_;
    AudioPolicy& audio_;
    Config& config_;
    bool paused_ = false;
    std::string uiUrl_;
};

}  // namespace canvas
