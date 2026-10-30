// Implementation of Controller declared in Controller.h. See the header for
// which live effect each change triggers.
#include "control/Controller.h"

#include <algorithm>
#include <set>

#include "audio/AudioPolicy.h"
#include "config/Config.h"
#include "core/EventLoop.h"
#include "core/Log.h"
#include "display/Platform.h"
#include "render/InstanceManager.h"
#include "render/Rotator.h"
#include "source/Playlist.h"
#include "source/SceneOptions.h"

namespace canvas {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

const std::set<std::string> kScalings = {"fill", "fit", "stretch", "default"};
const std::set<std::string> kLayers = {"background", "bottom"};
const std::set<std::string> kActions = {"next",   "pause",  "resume",
                                        "mute",   "unmute", "reload",
                                        "restart"};

bool hasPreview(const WallpaperInfo& info) {
    if (info.preview.empty()) return false;
    std::error_code ec;
    return fs::is_regular_file(info.dir / info.preview, ec);
}

json itemJson(const WallpaperInfo& info, const Config& config,
              bool broken) {
    auto ws = config.wallpapers.find(info.id);
    bool enabled = ws != config.wallpapers.end() && ws->second.enabled;
    bool customized = ws != config.wallpapers.end() &&
                      (!ws->second.properties.empty() ||
                       !ws->second.allowed.empty());
    return {{"id", info.id},
            {"title", info.title},
            {"type", toString(info.type)},
            {"reason", info.unsupportedReason},
            {"rating", info.contentRating},
            {"tags", info.tags},
            {"preview", hasPreview(info)},
            {"enabled", enabled},
            {"customized", customized},
            {"broken", broken}};
}

// Reads a string list from j[key] when present; false on a wrong type.
bool stringList(const json& j, const char* key,
                std::vector<std::string>& out, std::string& error) {
    if (!j.contains(key)) return true;
    if (!j[key].is_array()) {
        error = std::string(key) + " must be a list";
        return false;
    }
    out.clear();
    for (const auto& v : j[key]) {
        if (!v.is_string()) {
            error = std::string(key) + " entries must be strings";
            return false;
        }
        out.push_back(v.get<std::string>());
    }
    return true;
}

// Property values arrive from the UI as strings, numbers or booleans; keep
// them as text in lwe's --set-property format.
bool valueText(const json& v, std::string& out) {
    if (v.is_string()) out = v.get<std::string>();
    else if (v.is_boolean()) out = v.get<bool>() ? "true" : "false";
    else if (v.is_number_integer()) out = std::to_string(v.get<long long>());
    else if (v.is_number()) out = v.dump();
    else return false;
    return true;
}

}  // namespace

Controller::Controller(EventLoop& loop, Platform* platform,
                       InstanceManager& manager, Rotator& rotator,
                       AudioPolicy& audio, Config& config)
    : loop_(loop),
      platform_(platform),
      manager_(manager),
      rotator_(rotator),
      audio_(audio),
      config_(config) {}

bool Controller::save(std::string& error) {
    if (config_.save(config_.sourcePath)) return true;
    error = "could not write " + config_.sourcePath.string();
    return false;
}

json Controller::status() const {
    json outputs = json::array();
    auto running = manager_.running();
    if (platform_) {
        for (const auto& o : platform_->outputs()) {
            std::string id = running.count(o.name) ? running[o.name] : "";
            const WallpaperInfo* info = id.empty() ? nullptr : manager_.find(id);
            outputs.push_back({{"name", o.name},
                               {"width", o.width},
                               {"height", o.height},
                               {"id", id},
                               {"title", info ? info->title : ""},
                               {"assigned", config_.wallpaperFor(o.name)}});
        }
    }
    return {{"outputs", outputs},
            {"running", running},
            {"muted", config_.audioMuted},
            {"paused", paused_},
            {"rotation",
             {{"active", rotator_.active()},
              {"interval", config_.playlistInterval},
              {"outputs", config_.playlistOutputs},
              {"enabled", config_.enabledIds().size()},
              {"current", rotator_.current()},
              {"next_in", rotator_.secondsRemaining()}}},
            {"library_revision", manager_.libraryRevision()},
            {"library_size", manager_.items().size()},
            {"ui", uiUrl_}};
}

json Controller::library() const {
    json items = json::array();
    for (const auto& info : manager_.items()) {
        bool broken = std::find(config_.broken.begin(), config_.broken.end(),
                                info.id) != config_.broken.end();
        items.push_back(itemJson(info, config_, broken));
    }
    return items;
}

json Controller::settings() const {
    json roots = json::array();
    for (const auto& r : config_.contentRoots) roots.push_back(r.string());
    return {{"fps", config_.fps},
            {"scaling", config_.scaling},
            {"layer", config_.layer},
            {"audio",
             {{"muted", config_.audioMuted}, {"volume", config_.audioVolume}}},
            {"scene",
             {{"engine", config_.sceneEngine},
              {"native_available", !manager_.sceneHelperPath().empty()},
              {"engine_path", config_.sceneEnginePath},
              {"assets_dir", config_.sceneAssetsDir},
              {"disable_mouse", config_.sceneDisableMouse},
              {"randomize_options", config_.sceneRandomizeOptions},
              {"include_nsfw_options", config_.sceneIncludeNsfwOptions}}},
            {"web", {{"enabled", config_.webEnabled}}},
            {"content_roots", roots},
            {"assignments", config_.assignments},
            {"playlist",
             {{"interval", config_.playlistInterval},
              {"outputs", config_.playlistOutputs}}},
            {"ui",
             {{"enabled", config_.uiEnabled},
              {"port", config_.uiPort},
              {"blur_mature", config_.uiBlurMature}}},
            {"broken", config_.broken}};
}

bool Controller::updateSettings(const json& patch, std::string& error,
                                std::vector<std::string>& notes) {
    if (!patch.is_object()) {
        error = "settings must be an object";
        return false;
    }
    // Validate and stage into a copy first so a bad field changes nothing.
    Config next = config_;
    bool restart = false, rescan = false, rotation = false, rearm = false;
    bool volumeChanged = false;
    std::vector<std::string> unbroken;
    std::map<std::string, std::string> assignmentChanges;  // "" = remove
    try {
        if (patch.contains("fps")) {
            int fps = patch["fps"].get<int>();
            if (fps < 1 || fps > 240) {
                error = "fps must be 1..240";
                return false;
            }
            restart |= fps != next.fps;
            next.fps = fps;
        }
        if (patch.contains("scaling")) {
            auto s = patch["scaling"].get<std::string>();
            if (!kScalings.count(s)) {
                error = "scaling must be fill, fit, stretch or default";
                return false;
            }
            restart |= s != next.scaling;
            next.scaling = s;
        }
        if (patch.contains("layer")) {
            auto l = patch["layer"].get<std::string>();
            if (!kLayers.count(l)) {
                error = "layer must be background or bottom";
                return false;
            }
            restart |= l != next.layer;
            next.layer = l;
        }
        if (patch.contains("audio")) {
            const auto& a = patch["audio"];
            if (a.contains("volume")) {
                int v = a["volume"].get<int>();
                if (v < 0 || v > 100) {
                    error = "volume must be 0..100";
                    return false;
                }
                // Applied live after the commit (InstanceManager::setVolume
                // restarts only backends that cannot change it live).
                volumeChanged = v != next.audioVolume;
                next.audioVolume = v;
            }
            if (a.contains("muted")) next.audioMuted = a["muted"].get<bool>();
        }
        if (patch.contains("scene")) {
            const auto& s = patch["scene"];
            auto text = [&](const char* key, std::string& field) {
                if (!s.contains(key)) return;
                auto v = s[key].get<std::string>();
                restart |= v != field;
                field = v;
            };
            if (s.contains("engine")) {
                auto v = s["engine"].get<std::string>();
                if (!Config::validEngine(v)) {
                    error = "scene.engine must be lwe, native or auto";
                    return false;
                }
                restart |= v != next.sceneEngine;
                next.sceneEngine = v;
            }
            text("engine_path", next.sceneEnginePath);
            if (s.contains("assets_dir")) {
                auto v = s["assets_dir"].get<std::string>();
                rescan |= v != next.sceneAssetsDir;
                next.sceneAssetsDir = v;
            }
            if (s.contains("disable_mouse")) {
                bool v = s["disable_mouse"].get<bool>();
                restart |= v != next.sceneDisableMouse;
                next.sceneDisableMouse = v;
            }
            if (s.contains("randomize_options"))
                next.sceneRandomizeOptions = s["randomize_options"].get<bool>();
            if (s.contains("include_nsfw_options"))
                next.sceneIncludeNsfwOptions =
                    s["include_nsfw_options"].get<bool>();
        }
        if (patch.contains("web") && patch["web"].contains("enabled")) {
            bool v = patch["web"]["enabled"].get<bool>();
            restart |= v != next.webEnabled;
            next.webEnabled = v;
        }
        if (patch.contains("content_roots")) {
            std::vector<std::string> roots;
            if (!stringList(patch, "content_roots", roots, error)) return false;
            next.contentRoots.clear();
            for (const auto& r : roots)
                if (!r.empty()) next.contentRoots.push_back(Config::expandUser(r));
            rescan = true;
        }
        if (patch.contains("assignments")) {
            if (!patch["assignments"].is_object()) {
                error = "assignments must be an object";
                return false;
            }
            std::map<std::string, std::string> wanted;
            for (const auto& [output, id] : patch["assignments"].items())
                if (id.is_string() && !id.get<std::string>().empty())
                    wanted[output] = id.get<std::string>();
            for (const auto& [output, id] : wanted) {
                auto it = next.assignments.find(output);
                if (it == next.assignments.end() || it->second != id) {
                    const WallpaperInfo* info = manager_.find(id);
                    if (!info || info->type == WallpaperType::Unsupported) {
                        error = "wallpaper " + id + " is not usable";
                        return false;
                    }
                    assignmentChanges[output] = id;
                }
            }
            for (const auto& [output, id] : next.assignments)
                if (!wanted.count(output)) assignmentChanges[output] = "";
        }
        if (patch.contains("playlist")) {
            const auto& p = patch["playlist"];
            if (p.contains("interval")) {
                int i = p["interval"].get<int>();
                if (i < 0) {
                    error = "interval must be >= 0";
                    return false;
                }
                // Always refresh: "rotate 300" with an unchanged interval
                // must still (re)start an idle rotation.
                rotation = true;
                rearm = true;
                next.playlistInterval = i;
            }
            if (p.contains("outputs")) {
                if (!stringList(p, "outputs", next.playlistOutputs, error))
                    return false;
                rotation = true;
            }
        }
        if (patch.contains("ui")) {
            const auto& u = patch["ui"];
            if (u.contains("port")) {
                int port = u["port"].get<int>();
                if (port < 1024 || port > 65535) {
                    error = "port must be 1024..65535";
                    return false;
                }
                if (port != next.uiPort)
                    notes.push_back("the new UI port is used after the "
                                    "daemon restarts");
                next.uiPort = port;
            }
            if (u.contains("enabled")) {
                bool v = u["enabled"].get<bool>();
                if (v != next.uiEnabled)
                    notes.push_back("turning the UI on or off takes effect "
                                    "after the daemon restarts");
                next.uiEnabled = v;
            }
            if (u.contains("blur_mature"))
                next.uiBlurMature = u["blur_mature"].get<bool>();
        }
        if (patch.contains("broken")) {
            std::vector<std::string> keep;
            if (!stringList(patch, "broken", keep, error)) return false;
            for (const auto& id : next.broken)
                if (std::find(keep.begin(), keep.end(), id) == keep.end())
                    unbroken.push_back(id);
            // Only removal is supported; the daemon adds entries itself.
        }
    } catch (const json::exception&) {
        error = "a setting has the wrong type";
        return false;
    }

    // Commit. Mute, broken and assignments go through InstanceManager,
    // which persists them itself, so apply the rest first.
    bool mutedChanged = next.audioMuted != config_.audioMuted;
    bool newMuted = next.audioMuted;
    next.audioMuted = config_.audioMuted;
    next.broken = config_.broken;
    next.assignments = config_.assignments;
    config_ = next;
    if (!save(error)) return false;

    if (volumeChanged) manager_.setVolume(config_.audioVolume);
    if (mutedChanged) manager_.setMuted(newMuted);
    for (const auto& id : unbroken) manager_.clearBroken(id);
    for (const auto& [output, id] : assignmentChanges) {
        std::string e;
        if (!manager_.setAssignment(output, id, e))
            log::warn() << "assignment " << output << ": " << e;
    }
    if (rescan) manager_.rescan();
    if (restart || rescan) manager_.restartAll();
    if (rotation) {
        if (config_.playlistInterval <= 0) {
            rotator_.stop();
        } else {
            rotator_.refresh(rearm);
        }
    }
    return true;
}

bool Controller::wallpaper(const std::string& id, json& out,
                           std::string& error) const {
    const WallpaperInfo* info = manager_.find(id);
    if (!info) {
        error = "unknown wallpaper id " + id;
        return false;
    }
    bool broken = std::find(config_.broken.begin(), config_.broken.end(),
                            id) != config_.broken.end();
    out = itemJson(*info, config_, broken);
    json props = json::array();
    if (info->type == WallpaperType::Scene) {
        for (const auto& p : readUserProperties(info->dir)) {
            json options = json::array();
            for (const auto& o : p.options)
                options.push_back({{"label", o.label},
                                   {"value", o.value},
                                   {"nsfw", isNsfwOption(o)}});
            json prop = {{"name", p.name},
                         {"type", p.type},
                         {"text", p.text},
                         {"value", p.value},
                         {"order", p.order},
                         {"options", options}};
            if (p.hasRange) {
                prop["min"] = p.min;
                prop["max"] = p.max;
                prop["step"] = p.step;
            }
            props.push_back(prop);
        }
    }
    out["properties"] = props;
    auto ws = config_.wallpapers.find(id);
    out["pinned"] = ws != config_.wallpapers.end()
                        ? json(ws->second.properties)
                        : json::object();
    out["allowed"] = ws != config_.wallpapers.end()
                         ? json(ws->second.allowed)
                         : json::object();
    // Scene engine: the wallpaper's own choice ("" = global), what it
    // resolves to, and the native helper's recorded verdict, if any.
    out["engine"] = ws != config_.wallpapers.end() ? ws->second.engine : "";
    out["engine_effective"] = config_.engineFor(id);
    out["native"] = nullptr;
    if (info->type == WallpaperType::Scene) {
        const auto& verdicts = manager_.nativeSupport().all();
        auto v = verdicts.find(id);
        if (v != verdicts.end())
            out["native"] = {{"reason", v->second.reason},
                             {"features", v->second.features}};
        out["renderer"] =
            manager_.sceneEngineFor(*info) ==
                    InstanceManager::SceneEngine::Lwe
                ? "lwe"
                : "native";
    }
    std::vector<std::string> showingOn;
    for (const auto& [output, running] : manager_.running())
        if (running == id) showingOn.push_back(output);
    out["showing_on"] = showingOn;
    return true;
}

bool Controller::updateWallpaper(const std::string& id, const json& patch,
                                 std::string& error) {
    if (!manager_.find(id)) {
        error = "unknown wallpaper id " + id;
        return false;
    }
    if (!patch.is_object()) {
        error = "wallpaper settings must be an object";
        return false;
    }
    Config::WallpaperSettings next = config_.wallpapers[id];
    bool enabledChanged = false;
    bool engineChanged = false;
    bool retryNative = false;
    try {
        if (patch.contains("enabled")) {
            bool v = patch["enabled"].get<bool>();
            enabledChanged = v != next.enabled;
            next.enabled = v;
        }
        if (patch.contains("engine")) {
            auto v = patch["engine"].get<std::string>();
            if (!v.empty() && !Config::validEngine(v)) {
                error = "engine must be empty, lwe, native or auto";
                return false;
            }
            engineChanged = v != next.engine;
            next.engine = v;
        }
        // Forget the native helper's verdict so "auto" tries it again.
        retryNative = patch.value("retry_native", false);
        if (patch.contains("properties")) {
            if (!patch["properties"].is_object()) {
                error = "properties must be an object";
                return false;
            }
            next.properties.clear();
            for (const auto& [name, v] : patch["properties"].items()) {
                std::string text;
                if (!valueText(v, text)) {
                    error = "property " + name + " has an unsupported value";
                    return false;
                }
                next.properties[name] = text;
            }
        }
        if (patch.contains("allowed")) {
            if (!patch["allowed"].is_object()) {
                error = "allowed must be an object";
                return false;
            }
            next.allowed.clear();
            for (const auto& [name, values] : patch["allowed"].items()) {
                std::vector<std::string> list;
                if (!values.is_array()) {
                    error = "allowed." + name + " must be a list";
                    return false;
                }
                for (const auto& v : values) {
                    std::string text;
                    if (valueText(v, text)) list.push_back(text);
                }
                if (!list.empty()) next.allowed[name] = list;
            }
        }
    } catch (const json::exception&) {
        error = "a wallpaper setting has the wrong type";
        return false;
    }
    config_.wallpapers[id] = next;
    if (!save(error)) return false;
    if (enabledChanged) rotator_.refresh();
    if (retryNative) manager_.nativeSupport().forget(id);
    if (patch.value("apply", false) || engineChanged || retryNative)
        manager_.restartWallpaper(id);
    return true;
}

bool Controller::setEnabled(const std::vector<std::string>& ids,
                            bool enabled, std::string& error) {
    for (const auto& id : ids)
        if (!id.empty()) config_.wallpapers[id].enabled = enabled;
    if (!save(error)) return false;
    rotator_.refresh();
    return true;
}

bool Controller::show(const std::string& id, const std::string& output,
                      std::string& error) {
    if (!manager_.usable(id)) {
        error = "wallpaper " + id + " is not usable";
        return false;
    }
    manager_.setOverride(output.empty() ? "*" : output, id);
    manager_.apply();
    return true;
}

bool Controller::action(const std::string& name, std::string& error) {
    if (!kActions.count(name)) {
        error = "unknown action " + name;
        return false;
    }
    if (name == "next") {
        if (config_.enabledIds().empty()) {
            error = "no wallpapers are enabled for rotation";
            return false;
        }
        rotator_.advance();
    } else if (name == "pause") {
        manager_.pauseAll();
        paused_ = true;
    } else if (name == "resume") {
        manager_.resumeAll();
        paused_ = false;
    } else if (name == "mute" || name == "unmute") {
        manager_.setMuted(name == "mute");
    } else if (name == "reload") {
        manager_.rescan();
        manager_.apply();
    } else if (name == "restart") {
        manager_.restartAll();
    }
    return true;
}

std::string Controller::handleIpc(const std::string& line) {
    json req = json::parse(line, nullptr, false);
    if (req.is_discarded() || !req.is_object())
        return R"({"ok": false, "error": "malformed request"})";
    std::string cmd = req.value("cmd", "");
    json reply = {{"ok", true}};
    std::string error;
    auto fail = [&] { reply = {{"ok", false}, {"error", error}}; };

    if (cmd == "status") {
        json s = status();
        reply["running"] = s["running"];
        reply["enabled"] = s["rotation"]["enabled"];
        reply["interval"] = config_.playlistInterval;
        reply["rotating"] = rotator_.active();
        reply["next_in"] = rotator_.secondsRemaining();
        reply["rotate_outputs"] = config_.playlistOutputs;
        reply["ui"] = uiUrl_;
    } else if (cmd == "list") {
        reply["items"] = library();
    } else if (cmd == "set") {
        if (!manager_.setAssignment(req.value("output", "*"),
                                    req.value("id", ""), error))
            fail();
    } else if (cmd == "enable" || cmd == "disable") {
        std::vector<std::string> ids;
        if (!stringList(req, "ids", ids, error) ||
            !setEnabled(ids, cmd == "enable", error))
            fail();
    } else if (cmd == "playlist") {
        // Legacy: "file" imports an ids list into the enabled wallpapers
        // (additive); "" disables every wallpaper.
        std::string file = req.value("file", "");
        if (file.empty()) {
            if (!setEnabled(config_.enabledIds(), false, error)) fail();
        } else {
            Playlist list;
            if (!list.load(Config::expandUser(file))) {
                error = "cannot read " + file;
                fail();
            } else if (!setEnabled(list.ids(), true, error)) {
                fail();
            } else {
                reply["imported"] = list.size();
            }
        }
    } else if (cmd == "rotate") {
        json patch = {{"playlist",
                       {{"interval",
                         req.value("interval", config_.playlistInterval)}}}};
        if (req.contains("outputs")) patch["playlist"]["outputs"] = req["outputs"];
        std::vector<std::string> notes;
        if (!updateSettings(patch, error, notes)) {
            fail();
        } else if (config_.playlistInterval > 0 &&
                   config_.enabledIds().empty()) {
            error = "no wallpapers are enabled for rotation";
            fail();
        }
    } else if (cmd == "next") {
        if (!action("next", error)) {
            fail();
        } else {
            reply["id"] = rotator_.current();
        }
    } else if (kActions.count(cmd)) {
        if (!action(cmd, error)) fail();
    } else if (cmd == "quit") {
        loop_.quit(0);
    } else {
        error = "unknown command " + cmd;
        fail();
    }
    return reply.dump();
}

}  // namespace canvas
