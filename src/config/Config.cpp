// Implementation of Config declared in Config.h. See the header for the
// JSON schema and precedence rules.
#include "config/Config.h"

#include <cstdlib>
#include <fstream>

#include <nlohmann/json.hpp>

#include "core/Log.h"
#include "source/Playlist.h"

namespace canvas {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

// Property values are kept as text (lwe's --set-property takes text);
// accept numbers and booleans written by hand too.
std::string scalarText(const json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (v.is_number()) return v.dump();
    return "";
}

}  // namespace

fs::path Config::expandUser(const std::string& raw) {
    if (raw.empty()) return {};
    if (raw[0] != '~') return raw;
    if (raw.size() > 1 && raw[1] != '/') return raw;  // ~user is not supported
    const char* home = std::getenv("HOME");
    if (!home || !*home) return raw;
    return fs::path(home) / (raw.size() > 1 ? raw.substr(2) : "");
}

fs::path Config::defaultPath() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    fs::path base;
    if (xdg && *xdg) {
        base = xdg;
    } else {
        const char* home = std::getenv("HOME");
        base = fs::path(home ? home : ".") / ".config";
    }
    return base / "desktop-canvas" / "config.json";
}

Config Config::load(const fs::path& path) {
    Config cfg;
    cfg.sourcePath = path;
    std::ifstream in(path);
    if (!in) return cfg;

    json j = json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) {
        log::warn() << "config " << path << " is malformed, using defaults";
        return cfg;
    }
    for (const auto& r : j.value("content_roots", json::array()))
        if (r.is_string()) cfg.contentRoots.emplace_back(r.get<std::string>());
    if (j.contains("assignments") && j["assignments"].is_object())
        for (const auto& [k, v] : j["assignments"].items())
            if (v.is_string()) cfg.assignments[k] = v.get<std::string>();
    cfg.fps = j.value("fps", cfg.fps);
    cfg.scaling = j.value("scaling", cfg.scaling);
    cfg.layer = j.value("layer", cfg.layer);
    if (j.contains("audio") && j["audio"].is_object()) {
        cfg.audioMuted = j["audio"].value("muted", cfg.audioMuted);
        cfg.audioVolume = j["audio"].value("volume", cfg.audioVolume);
    }
    if (j.contains("scene") && j["scene"].is_object()) {
        cfg.sceneEnginePath = j["scene"].value("engine_path", "");
        cfg.sceneAssetsDir = j["scene"].value("assets_dir", "");
        cfg.sceneDisableMouse =
            j["scene"].value("disable_mouse", cfg.sceneDisableMouse);
        cfg.sceneRandomizeOptions = j["scene"].value(
            "randomize_options", cfg.sceneRandomizeOptions);
        cfg.sceneIncludeNsfwOptions = j["scene"].value(
            "include_nsfw_options", cfg.sceneIncludeNsfwOptions);
    }
    if (j.contains("web") && j["web"].is_object())
        cfg.webEnabled = j["web"].value("enabled", cfg.webEnabled);
    for (const auto& b : j.value("broken", json::array()))
        if (b.is_string()) cfg.broken.push_back(b.get<std::string>());
    fs::path legacyPlaylist;
    if (j.contains("playlist") && j["playlist"].is_object()) {
        const auto& p = j["playlist"];
        legacyPlaylist = expandUser(p.value("file", std::string()));
        cfg.playlistInterval = p.value("interval", cfg.playlistInterval);
        for (const auto& o : p.value("outputs", json::array()))
            if (o.is_string()) cfg.playlistOutputs.push_back(o.get<std::string>());
    }
    if (j.contains("wallpapers") && j["wallpapers"].is_object()) {
        for (const auto& [id, w] : j["wallpapers"].items()) {
            if (!w.is_object()) continue;
            WallpaperSettings s;
            s.enabled = w.value("enabled", false);
            if (w.contains("properties") && w["properties"].is_object())
                for (const auto& [name, v] : w["properties"].items())
                    s.properties[name] = scalarText(v);
            if (w.contains("allowed") && w["allowed"].is_object())
                for (const auto& [name, values] : w["allowed"].items()) {
                    if (!values.is_array()) continue;
                    auto& list = s.allowed[name];
                    for (const auto& v : values) list.push_back(scalarText(v));
                }
            cfg.wallpapers[id] = std::move(s);
        }
    }
    if (j.contains("ui") && j["ui"].is_object()) {
        cfg.uiEnabled = j["ui"].value("enabled", cfg.uiEnabled);
        cfg.uiPort = j["ui"].value("port", cfg.uiPort);
        cfg.uiBlurMature = j["ui"].value("blur_mature", cfg.uiBlurMature);
    }

    // One time import of the legacy external ids list. Only when nothing is
    // enabled yet, so a later save (which drops "file") cannot re-import.
    if (!legacyPlaylist.empty() && cfg.enabledIds().empty()) {
        Playlist legacy;
        if (legacy.load(legacyPlaylist)) {
            for (const auto& id : legacy.ids())
                cfg.wallpapers[id].enabled = true;
            cfg.importedPlaylistFrom = legacyPlaylist;
            log::info() << "imported " << legacy.size() << " id(s) from "
                        << legacyPlaylist << " into config wallpapers";
        }
    }
    return cfg;
}

std::vector<std::string> Config::enabledIds() const {
    std::vector<std::string> ids;
    for (const auto& [id, w] : wallpapers)  // map order: sorted
        if (w.enabled) ids.push_back(id);
    return ids;
}

bool Config::save(const fs::path& path) const {
    json j;
    j["content_roots"] = json::array();
    for (const auto& r : contentRoots) j["content_roots"].push_back(r.string());
    j["assignments"] = assignments;
    j["fps"] = fps;
    j["scaling"] = scaling;
    j["layer"] = layer;
    j["audio"] = {{"muted", audioMuted}, {"volume", audioVolume}};
    j["scene"] = {{"engine_path", sceneEnginePath},
                  {"assets_dir", sceneAssetsDir},
                  {"disable_mouse", sceneDisableMouse},
                  {"randomize_options", sceneRandomizeOptions},
                  {"include_nsfw_options", sceneIncludeNsfwOptions}};
    j["web"] = {{"enabled", webEnabled}};
    j["broken"] = broken;
    j["playlist"] = {{"interval", playlistInterval},
                     {"outputs", playlistOutputs}};
    j["wallpapers"] = json::object();
    for (const auto& [id, w] : wallpapers) {
        // Untouched entries (not enabled, nothing pinned or restricted) are
        // not worth persisting; dropping them keeps the file readable.
        if (!w.enabled && w.properties.empty() && w.allowed.empty()) continue;
        j["wallpapers"][id] = {{"enabled", w.enabled},
                               {"properties", w.properties},
                               {"allowed", w.allowed}};
    }
    j["ui"] = {{"enabled", uiEnabled},
               {"port", uiPort},
               {"blur_mature", uiBlurMature}};

    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp);
        if (!out) return false;
        out << j.dump(2) << "\n";
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        log::warn() << "config save failed: " << ec.message();
        return false;
    }
    return true;
}

std::string Config::wallpaperFor(const std::string& output) const {
    auto it = assignments.find(output);
    if (it != assignments.end()) return it->second;
    it = assignments.find("*");
    if (it != assignments.end()) return it->second;
    return "";
}

}  // namespace canvas
