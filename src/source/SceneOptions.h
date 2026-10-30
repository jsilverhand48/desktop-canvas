// Scene user properties and per launch variant selection.
//
// Wallpaper Engine scenes declare user properties in project.json:
//
//   "general": {"properties": {
//     "mode_combo": {"type": "combo", "text": "Mode", "order": 1,
//                    "value": "dual",
//                    "options": [{"label": "A. Dual Panel", "value": "dual"},
//                                {"label": "B. Single Panel",
//                                 "value": "stretched"}]},
//     "speed": {"type": "slider", "value": 3, "min": 0, "max": 10,
//               "step": 1},
//     "clock": {"type": "bool", "value": false},
//     "tint":  {"type": "color", "value": "1 0.5 0"}}}
//
// readUserProperties() returns all of them (for the settings UI and for
// picking). resolveSceneProperties() turns a wallpaper's saved choices into
// the `--set-property name=value` list passed to lwe (LweOptions).
//
// Contract:
// - Reading is tolerant: a missing or malformed project.json, or malformed
//   entries, yield fewer (or no) properties, never an error. Values are
//   kept as text in lwe's format: booleans "true"/"false", numbers as
//   written, colours "r g b" (0..1).
// - Pinned values are always emitted and win over random picks.
// - With randomize on, each other dropdown gets one random value from its
//   eligible options: the wallpaper's "allowed" list for it when present
//   (an NSFW value the user explicitly allowed is honoured), otherwise every
//   option minus those labelled or valued "nsfw"/"nude" (case insensitive)
//   unless includeNsfw is true.
// - A dropdown with nothing to vary (one option, nothing filtered) or with
//   no eligible option is left at the wallpaper default. When filtering
//   leaves exactly one option it is still set, so an excluded default
//   (e.g. NSFW) cannot show.
// - Output is sorted by property name, so a given RNG state gives a
//   deterministic result.
#pragma once

#include <filesystem>
#include <map>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace canvas {

struct ComboOption {
    std::string label;
    std::string value;
};

struct UserProperty {
    std::string name;
    std::string type;   // lower case: combo, slider, bool, color, textinput,
                        // text (a heading, no value), file, ...
    std::string text;   // label shown in Wallpaper Engine (may be a
                        // ui_... localisation key)
    std::string value;  // default, as text (see contract)
    int order = 0;
    std::vector<ComboOption> options;  // combo only
    double min = 0, max = 0, step = 0;  // slider only
    bool hasRange = false;
};

using PropertyList = std::vector<std::pair<std::string, std::string>>;

// User properties declared in <wallpaperDir>/project.json, sorted by
// (order, name).
std::vector<UserProperty> readUserProperties(
    const std::filesystem::path& wallpaperDir);

// True when the option looks explicit ("nsfw" or "nude" in label or value).
bool isNsfwOption(const ComboOption& option);

// The --set-property list for one launch (see contract).
PropertyList resolveSceneProperties(
    const std::vector<UserProperty>& properties,
    const std::map<std::string, std::string>& pinned,
    const std::map<std::string, std::vector<std::string>>& allowed,
    bool randomize, bool includeNsfw, std::mt19937& rng);

}  // namespace canvas
