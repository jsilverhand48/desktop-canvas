// User properties of project.json, per src/scene/model/README.md sections
// 3 and 10: declarations, the value text form, conversions from it, and
// display conditions.
//
// Built as its own library, canvas_scene_props, with no dependency beyond
// the JSON header, because both the daemon (src/source/SceneOptions: the
// settings UI and linux-wallpaperengine's --set-property) and the scene
// helper (bindings, scripts) use it; the daemon does not link the engine.
//
// Contract:
// - Parsing is tolerant: malformed entries are skipped, a missing or
//   malformed project.json gives no properties. Labels (entries without a
//   type) are kept with an empty type; callers that only want settings
//   filter them.
// - Values travel as text (section 3.2): booleans true/false, numbers in
//   decimal (integers without a decimal point), colours "r g b", strings
//   as is. valueText() makes that text from JSON, the textTo* functions
//   read it back.
// - evaluateCondition() never runs JavaScript; it understands the small
//   expression language of section 3.3 and shows the property (true) when
//   an expression does not parse.
#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace canvas::scene {

struct ComboOption {
    std::string label;
    std::string value;
};

struct UserProperty {
    std::string name;       // the key bindings and conditions use
    std::string type;       // lower case as declared: bool, color, slider,
                            // combo, textinput, text, group, scenetexture, ...;
                            // empty for labels
    std::string text;       // label shown (may be a ui_ localisation key)
    std::string value;      // declared default in the text form; empty
                            // when none (headings may declare one: unused)
    int order = 0;
    int index = -1;         // -1 when absent
    std::vector<ComboOption> options;   // combo
    double min = 0, max = 0, step = 0;  // slider
    bool hasRange = false;              // slider declared min and max
    int precision = -1;                 // slider, -1 when absent
    bool fraction = false;              // slider
    std::string condition;              // display condition, empty when none
};

// Property name to value text.
using PropertyValues = std::map<std::string, std::string>;

// Every entry of general.properties (labels included), sorted by
// (order, name).
std::vector<UserProperty> parseUserProperties(const nlohmann::json& project);
// The same from <wallpaperDir>/project.json.
std::vector<UserProperty> readUserProperties(const std::filesystem::path& wallpaperDir);

// The defaults of every setting (labels, text and group headings skipped).
PropertyValues defaultValues(const std::vector<UserProperty>& properties);

// JSON scalar to the text form; false for null, arrays and objects.
bool valueText(const nlohmann::json& value, std::string& out);

// Text form readers. textToBool: true/1 and false/0, other numbers by
// being non zero. textToNumber: decimal numbers, true/false as 1/0.
std::optional<bool> textToBool(std::string_view text);
std::optional<double> textToNumber(std::string_view text);
// Up to maxCount numbers separated by spaces or commas into out; returns
// how many were read, or -1 when a component is not a number or there are
// none.
int textToFloats(std::string_view text, float* out, int maxCount);

// Display condition (section 3.3) against the properties' current values
// (defaults where values has no entry).
bool evaluateCondition(std::string_view expression,
                       const std::vector<UserProperty>& properties,
                       const PropertyValues& values);

}  // namespace canvas::scene
