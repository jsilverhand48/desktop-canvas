// Implementation of the value conversions declared in Field.h.
#include "scene/model/Field.h"

#include <cmath>

namespace canvas::scene {

using nlohmann::json;

namespace {

// Vector from text: one number sets every component, more fill from the
// left and keep defaults for the rest.
template <size_t N>
bool vectorFromText(std::string_view text, std::array<float, N>& out) {
    float parts[N];
    int count = textToFloats(text, parts, int(N));
    if (count < 0) return false;
    if (count == 1) out.fill(parts[0]);
    else
        for (int i = 0; i < count; i++) out[size_t(i)] = parts[i];
    return true;
}

template <size_t N>
bool vectorFromJson(const json& v, std::array<float, N>& out) {
    if (v.is_number() && !v.is_boolean()) {
        out.fill(v.get<float>());
        return std::isfinite(out[0]);
    }
    if (v.is_string()) return vectorFromText(v.get_ref<const std::string&>(), out);
    return false;
}

}  // namespace

bool readPlain(const json& v, bool& out) {
    if (v.is_boolean()) {
        out = v.get<bool>();
        return true;
    }
    if (v.is_number()) {
        out = v.get<double>() != 0;
        return true;
    }
    if (v.is_string()) return readText(v.get_ref<const std::string&>(), out);
    return false;
}

bool readPlain(const json& v, float& out) {
    if (v.is_number() && !v.is_boolean()) {
        double d = v.get<double>();
        if (!std::isfinite(d)) return false;
        out = static_cast<float>(d);
        return true;
    }
    if (v.is_boolean()) {
        out = v.get<bool>() ? 1.0f : 0.0f;
        return true;
    }
    if (v.is_string()) return readText(v.get_ref<const std::string&>(), out);
    return false;
}

bool readPlain(const json& v, int64_t& out) {
    if (v.is_number_integer()) {
        out = v.get<int64_t>();
        return true;
    }
    if (v.is_number_float()) {
        double d = v.get<double>();
        if (!std::isfinite(d) || d != std::floor(d) || std::fabs(d) > 9.0e15) return false;
        out = static_cast<int64_t>(d);
        return true;
    }
    if (v.is_string()) return readText(v.get_ref<const std::string&>(), out);
    return false;
}

bool readPlain(const json& v, std::string& out) {
    return valueText(v, out);
}

bool readPlain(const json& v, Vec2& out) { return vectorFromJson(v, out); }
bool readPlain(const json& v, Vec3& out) { return vectorFromJson(v, out); }
bool readPlain(const json& v, Vec4& out) { return vectorFromJson(v, out); }

bool readPlain(const json& v, Floats& out) {
    if (v.is_number() && !v.is_boolean()) {
        float f = v.get<float>();
        if (!std::isfinite(f)) return false;
        out = Floats{{f, 0, 0, 0}, 1};
        return true;
    }
    if (v.is_string()) return readText(v.get_ref<const std::string&>(), out);
    return false;
}

bool readText(std::string_view text, bool& out) {
    auto b = textToBool(text);
    if (!b) return false;
    out = *b;
    return true;
}

bool readText(std::string_view text, float& out) {
    auto n = textToNumber(text);
    if (!n) return false;
    out = static_cast<float>(*n);
    return true;
}

bool readText(std::string_view text, int64_t& out) {
    auto n = textToNumber(text);
    if (!n || *n != std::floor(*n) || std::fabs(*n) > 9.0e15) return false;
    out = static_cast<int64_t>(*n);
    return true;
}

bool readText(std::string_view text, std::string& out) {
    out = std::string(text);
    return true;
}

bool readText(std::string_view text, Vec2& out) { return vectorFromText(text, out); }
bool readText(std::string_view text, Vec3& out) { return vectorFromText(text, out); }
bool readText(std::string_view text, Vec4& out) { return vectorFromText(text, out); }

bool readText(std::string_view text, Floats& out) {
    Floats f;
    int count = textToFloats(text, f.v.data(), 4);
    if (count < 0) return false;
    f.count = count;
    out = f;
    return true;
}

bool isWrapped(const json& v) {
    if (!v.is_object()) return false;
    for (const char* key : {"value", "user", "script", "scriptproperties", "animation"})
        if (v.contains(key)) return true;
    return false;
}

std::shared_ptr<const Dynamics> readDynamics(const json& wrapped, LoadReport& report,
                                             const std::string& context) {
    auto d = std::make_shared<Dynamics>();
    bool any = false;
    if (auto user = wrapped.find("user"); user != wrapped.end()) {
        if (user->is_string()) {
            d->binding = Binding{user->get<std::string>(), std::nullopt};
        } else if (user->is_object()) {
            auto name = user->find("name");
            if (name != user->end() && name->is_string()) {
                Binding b{name->get<std::string>(), std::nullopt};
                std::string condition;
                if (auto c = user->find("condition"); c != user->end() && valueText(*c, condition))
                    b.condition = condition;
                d->binding = std::move(b);
            } else {
                report.warn(context + ": binding without a property name");
            }
        } else if (!user->is_null()) {
            report.warn(context + ": unexpected binding " + std::string(user->type_name()));
        }
        any |= d->binding.has_value();
    }
    if (auto script = wrapped.find("script"); script != wrapped.end() && script->is_string()) {
        d->script = script->get<std::string>();
        any |= !d->script.empty();
    }
    if (auto props = wrapped.find("scriptproperties"); props != wrapped.end())
        d->scriptProperties = *props;
    if (auto animation = wrapped.find("animation");
        animation != wrapped.end() && !animation->is_null()) {
        d->animation = *animation;
        any = true;
    }
    if (!any) return nullptr;
    return d;
}

bool conditionMatches(std::string_view value, std::string_view condition) {
    if (value == condition) return true;
    auto a = textToNumber(value);
    auto b = textToNumber(condition);
    // "true"/"false" read as numbers too; only compare real numbers here.
    if (a && b && value != "true" && value != "false" && condition != "true" &&
        condition != "false")
        return *a == *b;
    return false;
}

}  // namespace canvas::scene
