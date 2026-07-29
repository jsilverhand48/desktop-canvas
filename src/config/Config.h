// Daemon configuration, persisted as JSON at
// $XDG_CONFIG_HOME/desktop-canvas/config.json (default ~/.config/...).
//
// Precedence: built in defaults < config file < command line flags. The
// daemon rewrites the file when assignments change through IPC so state
// survives restarts.
//
// Schema (all keys optional):
// {
//   "content_roots": ["/path/to/431960", ...],   // empty = autodetect
//   "assignments": {"DP-1": "1049326028", "*": "1093126365"},
//   "fps": 30,
//   "scaling": "fill",                            // fill | fit | stretch
//   "layer": "background",                        // background | bottom
//   "audio": {"muted": true, "volume": 15},
//   "scene": {"engine_path": "", "assets_dir": "", "disable_mouse": false},
//   "web": {"enabled": false},
//   "broken": ["<wallpaper id>", ...],            // auto blacklist
//   "playlist": {
//     "file": "~/.config/wallpaper-engine/ids.txt",  // "" = no playlist
//     "interval": 900,                              // seconds, 0 = no rotation
//     "outputs": ["DP-5"]                           // [] = every output
//   }
// }
//
// The playlist block holds rotation *settings* only. The wallpaper a
// rotation tick picks is deliberately not written here: it lives in
// InstanceManager's runtime override map, so "assignments" keeps meaning the
// user's own choice and the config file is not rewritten every interval.
#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace canvas {

struct Config {
    std::vector<std::filesystem::path> contentRoots;  // empty = autodetect
    std::map<std::string, std::string> assignments;   // output name -> id
    int fps = 30;
    std::string scaling = "fill";
    std::string layer = "background";
    bool audioMuted = true;   // requirement: audio defaults to muted
    int audioVolume = 15;
    std::string sceneEnginePath;  // empty = search PATH
    std::string sceneAssetsDir;   // empty = autodetect from content roots
    bool sceneDisableMouse = false;
    bool webEnabled = false;
    std::vector<std::string> broken;  // wallpaper ids marked broken

    // Rotation: draw a random id from playlistFile every playlistInterval
    // seconds. All rotating outputs show the same pick; playlistOutputs
    // narrows which outputs rotate at all (empty = all of them).
    std::filesystem::path playlistFile;
    int playlistInterval = 0;  // seconds; <= 0 disables rotation
    std::vector<std::string> playlistOutputs;

    // Where this config was loaded from; runtime saves go back here so a
    // daemon started with --config never writes the default location.
    std::filesystem::path sourcePath;

    // Path of the config file honoring XDG_CONFIG_HOME.
    static std::filesystem::path defaultPath();

    // Expands a leading "~/" against $HOME so hand written config paths and
    // CLI arguments behave the way a shell would. "~user" is not expanded.
    static std::filesystem::path expandUser(const std::string& raw);

    // Loads from path if it exists; missing file returns defaults. Malformed
    // JSON logs a warning and returns defaults rather than aborting.
    static Config load(const std::filesystem::path& path);

    // Serializes and writes atomically (temp file + rename).
    bool save(const std::filesystem::path& path) const;

    // Wallpaper id assigned to an output: exact match, else "*", else empty.
    std::string wallpaperFor(const std::string& output) const;
};

}  // namespace canvas
