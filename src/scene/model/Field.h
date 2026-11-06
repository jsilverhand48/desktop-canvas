// Values of scene files, per src/scene/model/README.md section 2: plain
// values with the vector rules of 2.1, wrapped values (2.2) and user
// property bindings (2.3).
//
// A Field<T> holds one value. `stored` is what the file says, or the
// field's default when the key is absent, null or of the wrong type;
// `value` is the current value, which differs from `stored` only for a
// bound field after resolve(). Scripts, script properties and timeline
// animations stay unevaluated in `dynamics` for the phases that run them
// (P6a, P4b). T is bool, float, int64_t, std::string, Vec2, Vec3, Vec4 or
// Floats; readPlain() and readText() convert for each of them.
//
// Structs made of fields expose forEachField(f), calling f on every
// Field they contain, so binding resolution and feature scans walk a
// scene without keeping pointers into it.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

#include "scene/model/LoadReport.h"
#include "scene/model/UserProperties.h"

namespace canvas::scene {

using Vec2 = std::array<float, 2>;
using Vec3 = std::array<float, 3>;
using Vec4 = std::array<float, 4>;

// One to four floats whose width only the shader knows (effect constants).
struct Floats {
    std::array<float, 4> v{};
    int count = 0;
    bool operator==(const Floats&) const = default;
};

struct Binding {
    std::string property;
    std::optional<std::string> condition;  // set for conditional bindings
};

struct Dynamics {
    std::optional<Binding> binding;
    std::string script;                // empty without a script
    nlohmann::json scriptProperties;   // null without
    nlohmann::json animation;          // null without
};

template <class T>
struct Field {
    T value{};
    T stored{};
    bool present = false;  // the key was there and not null
    std::shared_ptr<const Dynamics> dynamics;  // null for plain values

    Field() = default;
    Field(T def) : value(def), stored(def) {}  // NOLINT: defaults read best implicit

    bool bound() const { return dynamics && dynamics->binding.has_value(); }
    bool scripted() const { return dynamics && !dynamics->script.empty(); }
    bool animated() const { return dynamics && !dynamics->animation.is_null(); }
};

// Plain JSON to T (section 2.1). `out` holds the default on entry; vector
// components the input lacks keep it. False when the type does not fit.
bool readPlain(const nlohmann::json& v, bool& out);
bool readPlain(const nlohmann::json& v, float& out);
bool readPlain(const nlohmann::json& v, int64_t& out);
bool readPlain(const nlohmann::json& v, std::string& out);
bool readPlain(const nlohmann::json& v, Vec2& out);
bool readPlain(const nlohmann::json& v, Vec3& out);
bool readPlain(const nlohmann::json& v, Vec4& out);
bool readPlain(const nlohmann::json& v, Floats& out);

// Property value text to T, for bindings (section 2.3); same conventions.
bool readText(std::string_view text, bool& out);
bool readText(std::string_view text, float& out);
bool readText(std::string_view text, int64_t& out);
bool readText(std::string_view text, std::string& out);
bool readText(std::string_view text, Vec2& out);
bool readText(std::string_view text, Vec3& out);
bool readText(std::string_view text, Vec4& out);
bool readText(std::string_view text, Floats& out);

// True for an object that is a wrapped value rather than a plain one.
bool isWrapped(const nlohmann::json& v);
// The binding, script and animation parts of a wrapped value; null when
// it has none of them.
std::shared_ptr<const Dynamics> readDynamics(const nlohmann::json& wrapped, LoadReport& report,
                                             const std::string& context);
// Conditional binding test: equal as text, or as numbers when both are.
bool conditionMatches(std::string_view value, std::string_view condition);

// Reads a (possibly wrapped) value with default def. Type problems keep
// the default and add a warning naming context.
template <class T>
Field<T> readValue(const nlohmann::json& v, T def, LoadReport& report,
                   const std::string& context) {
    Field<T> field(def);
    if (v.is_null()) return field;
    const nlohmann::json* plain = &v;
    if (isWrapped(v)) {
        field.dynamics = readDynamics(v, report, context);
        auto inner = v.find("value");
        plain = inner != v.end() ? &*inner : nullptr;
    }
    field.present = true;
    if (plain && !plain->is_null()) {
        T read = def;
        if (readPlain(*plain, read)) {
            field.stored = field.value = read;
        } else {
            report.warn(context + ": unexpected " + plain->type_name() + ", default kept");
        }
    }
    return field;
}

template <class T>
Field<T> readField(const nlohmann::json& object, const char* key, T def, LoadReport& report,
                   const std::string& context) {
    if (!object.is_object()) return Field<T>(def);
    auto it = object.find(key);
    if (it == object.end()) return Field<T>(def);
    return readValue(*it, def, report, context + "." + key);
}

// Sets a bound field's value from the current property values; plain
// fields are left alone. A plain binding to an unknown property keeps the
// stored value; a conditional one resolves as unmet (false / 0). With a
// report, failed conversions are warned.
template <class T>
void resolve(Field<T>& field, const PropertyValues& values, LoadReport* report = nullptr) {
    if (!field.bound()) return;
    const Binding& binding = *field.dynamics->binding;
    field.value = field.stored;
    auto it = values.find(binding.property);
    // Unknown property: a plain binding keeps the stored value, a
    // conditional one is unmet (README section 2.3).
    if (it == values.end() && !binding.condition) return;
    if (binding.condition) {
        bool match = it != values.end() && conditionMatches(it->second, *binding.condition);
        if constexpr (std::is_same_v<T, bool>) field.value = match;
        else if constexpr (std::is_same_v<T, float>) field.value = match ? 1.0f : 0.0f;
        return;
    }
    T converted = field.stored;
    if (readText(it->second, converted)) field.value = converted;
    else if (report)
        report->warn("property " + binding.property + " value '" + it->second +
                     "' does not fit its binding, stored value kept");
}

}  // namespace canvas::scene
