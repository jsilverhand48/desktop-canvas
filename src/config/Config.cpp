// Implementation of Config declared in Config.h. See the header for the
// JSON schema and precedence rules.
#include "config/Config.h"

#include <cstdlib>
#include <fstream>

#include <nlohmann/json.hpp>

#include "core/Log.h"

namespace canvas {

namespace fs = std::filesystem;
using nlohmann::json;

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
    }
    if (j.contains("web") && j["web"].is_object())
        cfg.webEnabled = j["web"].value("enabled", cfg.webEnabled);
    for (const auto& b : j.value("broken", json::array()))
        if (b.is_string()) cfg.broken.push_back(b.get<std::string>());
    if (j.contains("playlist") && j["playlist"].is_object()) {
        const auto& p = j["playlist"];
        cfg.playlistFile = expandUser(p.value("file", std::string()));
        cfg.playlistInterval = p.value("interval", cfg.playlistInterval);
        for (const auto& o : p.value("outputs", json::array()))
            if (o.is_string()) cfg.playlistOutputs.push_back(o.get<std::string>());
    }
    return cfg;
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
                  {"disable_mouse", sceneDisableMouse}};
    j["web"] = {{"enabled", webEnabled}};
    j["broken"] = broken;
    j["playlist"] = {{"file", playlistFile.string()},
                     {"interval", playlistInterval},
                     {"outputs", playlistOutputs}};

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
