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
//   "scene": {"engine_path": "", "assets_dir": "", "disable_mouse": false,
//             "randomize_options": false,     // random dropdown values
//             "include_nsfw_options": false}, // ...including NSFW ones
//   "web": {"enabled": false},
//   "broken": ["<wallpaper id>", ...],            // auto blacklist
//   "playlist": {
//     "interval": 900,                              // seconds, 0 = no rotation
//     "outputs": ["DP-5"]                           // [] = every output
//   },
//   "wallpapers": {                               // per wallpaper choices
//     "3826693905": {
//       "enabled": true,                          // in the rotation pool
//       "properties": {"mode_combo": "dual"},     // pinned --set-property
//       "allowed": {"style_left": ["valueA", "valueB"]}  // random pool
//     }
//   },
//   "ui": {"enabled": true, "port": 8765,         // http://127.0.0.1:port
//          "blur_mature": true}                   // blur rated previews
// }
//
// The rotation pool is every wallpaper with "enabled": true. Older configs
// pointed "playlist.file" at an external ids list; load() imports it once
// into "wallpapers" (see importedPlaylistFrom) and the key is not written
// back.
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
    // Pick a random value for every dropdown (combo) property of a scene at
    // each launch (poses, outfits, layout modes). See source/SceneOptions.h.
    bool sceneRandomizeOptions = false;
    // Let those random picks include options labelled NSFW / nude.
    bool sceneIncludeNsfwOptions = false;
    bool webEnabled = false;
    std::vector<std::string> broken;  // wallpaper ids marked broken

    // Per wallpaper choices made in the settings UI (or by hand).
    struct WallpaperSettings {
        // In the rotation pool. Replaces the old external ids.txt.
        bool enabled = false;
        // Pinned user property values, always passed to lwe as
        // --set-property name=value (they win over random picks).
        std::map<std::string, std::string> properties;
        // Per dropdown, the option values random picks may use. A dropdown
        // missing here uses every option (minus NSFW unless allowed).
        std::map<std::string, std::vector<std::string>> allowed;
    };
    std::map<std::string, WallpaperSettings> wallpapers;  // key: workshop id

    // Ids with enabled == true, sorted: the rotation pool.
    std::vector<std::string> enabledIds() const;

    // Rotation: draw a random enabled wallpaper every playlistInterval
    // seconds. All rotating outputs show the same pick; playlistOutputs
    // narrows which outputs rotate at all (empty = all of them).
    int playlistInterval = 0;  // seconds; <= 0 disables rotation
    std::vector<std::string> playlistOutputs;

    // Browser settings UI served by the daemon on 127.0.0.1:uiPort.
    bool uiEnabled = true;
    int uiPort = 8765;
    // Blur previews of Mature / Questionable items until clicked.
    bool uiBlurMature = true;

    // Set by load() when it imported a legacy "playlist.file" ids list into
    // `wallpapers` (enabled = true). The caller saves so the import happens
    // once; the old file itself is left alone.
    std::filesystem::path importedPlaylistFrom;

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
