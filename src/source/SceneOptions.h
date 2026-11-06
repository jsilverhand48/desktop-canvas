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
// readUserProperties() returns them (for the settings UI and for picking).
// resolveSceneProperties() turns a wallpaper's saved choices into the
// `--set-property name=value` list passed to the scene helper.
//
// Parsing lives in scene/model/UserProperties (library canvas_scene_props),
// shared with the native scene engine so both read properties the same
// way; UserProperty and ComboOption here are those types.
//
// Contract:
// - Reading is tolerant: a missing or malformed project.json, or malformed
//   entries, yield fewer (or no) properties, never an error. Labels (no
//   type) and combos without options are left out here. Values are kept as
//   text in the helper's format: booleans "true"/"false", numbers as written,
//   colours "r g b" (0..1).
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

#include "scene/model/UserProperties.h"

namespace canvas {

using ComboOption = scene::ComboOption;
using UserProperty = scene::UserProperty;

using PropertyList = std::vector<std::pair<std::string, std::string>>;

// User properties declared in <wallpaperDir>/project.json that the
// settings offer (see contract), sorted by (order, name).
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
