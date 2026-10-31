// Implementation of the user property functions declared in
// UserProperties.h.
#include "scene/model/UserProperties.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <fstream>

namespace canvas::scene {

using nlohmann::json;

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

std::optional<double> parseDouble(std::string_view s) {
    s = trim(s);
    if (!s.empty() && s.front() == '+') s.remove_prefix(1);
    if (s.empty()) return std::nullopt;
    double value = 0;
    auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc{} || end != s.data() + s.size() || !std::isfinite(value))
        return std::nullopt;
    return value;
}

double number(const json& obj, const char* key) {
    auto it = obj.find(key);
    return it != obj.end() && it->is_number() ? it->get<double>() : 0.0;
}

// --- display conditions ------------------------------------------------

struct CondValue {
    enum Kind { Undefined, Bool, Number, Text } kind = Undefined;
    bool b = false;
    double n = 0;
    std::string s;
};

bool truthy(const CondValue& v) {
    switch (v.kind) {
        case CondValue::Bool: return v.b;
        case CondValue::Number: return v.n != 0;
        case CondValue::Text: return !v.s.empty();
        default: return false;
    }
}

std::optional<double> asNumber(const CondValue& v) {
    if (v.kind == CondValue::Number) return v.n;
    if (v.kind == CondValue::Text) return parseDouble(v.s);
    return std::nullopt;
}

std::string asText(const CondValue& v) {
    switch (v.kind) {
        case CondValue::Bool: return v.b ? "true" : "false";
        case CondValue::Number: {
            if (v.n == std::floor(v.n) && std::fabs(v.n) < 1e15)
                return std::to_string(static_cast<long long>(v.n));
            return json(v.n).dump();
        }
        case CondValue::Text: return v.s;
        default: return "";
    }
}

// The loose equality of section 3.3.
bool looseEqual(const CondValue& a, const CondValue& b) {
    if (a.kind == CondValue::Undefined || b.kind == CondValue::Undefined) return false;
    if (a.kind == CondValue::Bool && b.kind == CondValue::Bool) return a.b == b.b;
    if (a.kind == CondValue::Bool || b.kind == CondValue::Bool) {
        const CondValue& flag = a.kind == CondValue::Bool ? a : b;
        const CondValue& other = a.kind == CondValue::Bool ? b : a;
        if (auto n = asNumber(other)) return *n == (flag.b ? 1.0 : 0.0);
        return asText(flag) == asText(other);
    }
    auto na = asNumber(a);
    auto nb = asNumber(b);
    if (na && nb) return *na == *nb;
    return asText(a) == asText(b);
}

class ConditionParser {
public:
    ConditionParser(std::string_view text, const std::vector<UserProperty>& properties,
                    const PropertyValues& values)
        : text_(text), properties_(properties), values_(values) {}

    // nullopt when the expression does not parse.
    std::optional<bool> run() {
        CondValue v = parseOr();
        skipSpace();
        if (failed_ || pos_ != text_.size()) return std::nullopt;
        return truthy(v);
    }

private:
    void skipSpace() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) pos_++;
    }
    bool accept(std::string_view token) {
        skipSpace();
        if (text_.substr(pos_, token.size()) != token) return false;
        pos_ += token.size();
        return true;
    }
    CondValue fail() {
        failed_ = true;
        return {};
    }
    CondValue boolean(bool b) {
        CondValue v;
        v.kind = CondValue::Bool;
        v.b = b;
        return v;
    }

    CondValue parseOr() {
        CondValue left = parseAnd();
        while (!failed_ && accept("||")) {
            CondValue right = parseAnd();
            left = boolean(truthy(left) || truthy(right));
        }
        return left;
    }
    CondValue parseAnd() {
        CondValue left = parseUnary();
        while (!failed_ && accept("&&")) {
            CondValue right = parseUnary();
            left = boolean(truthy(left) && truthy(right));
        }
        return left;
    }
    CondValue parseUnary() {
        skipSpace();
        // "!" but not the start of "!=" (which cannot begin an operand anyway).
        if (pos_ < text_.size() && text_[pos_] == '!' &&
            text_.substr(pos_, 2) != "!=") {
            pos_++;
            CondValue v = parseUnary();
            return boolean(!truthy(v));
        }
        return parseComparison();
    }
    CondValue parseComparison() {
        CondValue left = parsePrimary();
        if (failed_) return left;
        for (auto [token, negate] : {std::pair{"===", false}, {"!==", true}, {"==", false},
                                     {"!=", true}}) {
            if (accept(token)) {
                CondValue right = parsePrimary();
                bool eq = looseEqual(left, right);
                return boolean(negate ? !eq : eq);
            }
        }
        return left;
    }
    CondValue parsePrimary() {
        skipSpace();
        if (pos_ >= text_.size()) return fail();
        char c = text_[pos_];
        if (c == '(') {
            pos_++;
            CondValue v = parseOr();
            if (!accept(")")) return fail();
            return v;
        }
        if (c == '\'' || c == '"') {
            size_t end = text_.find(c, pos_ + 1);
            if (end == std::string_view::npos) return fail();
            CondValue v;
            v.kind = CondValue::Text;
            v.s = std::string(text_.substr(pos_ + 1, end - pos_ - 1));
            pos_ = end + 1;
            return v;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '.') {
            size_t start = pos_;
            pos_++;
            while (pos_ < text_.size() &&
                   (std::isdigit(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '.' ||
                    text_[pos_] == 'e' || text_[pos_] == 'E'))
                pos_++;
            auto n = parseDouble(text_.substr(start, pos_ - start));
            if (!n) return fail();
            CondValue v;
            v.kind = CondValue::Number;
            v.n = *n;
            return v;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$') {
            size_t start = pos_;
            while (pos_ < text_.size() &&
                   (std::isalnum(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '_' ||
                    text_[pos_] == '$'))
                pos_++;
            std::string name(text_.substr(start, pos_ - start));
            if (name == "true") return boolean(true);
            if (name == "false") return boolean(false);
            if (accept(".")) {  // name.value
                size_t field = pos_;
                while (pos_ < text_.size() &&
                       (std::isalnum(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '_'))
                    pos_++;
                if (text_.substr(field, pos_ - field) != "value") return CondValue{};
            }
            return lookup(name);
        }
        return fail();
    }

    // A property's current value, typed by its declaration.
    CondValue lookup(const std::string& name) const {
        auto prop = std::find_if(properties_.begin(), properties_.end(),
                                 [&](const UserProperty& p) { return p.name == name; });
        if (prop == properties_.end() || prop->type.empty()) return {};
        auto chosen = values_.find(name);
        const std::string& text = chosen != values_.end() ? chosen->second : prop->value;
        CondValue v;
        if (prop->type == "bool") {
            auto b = textToBool(text);
            if (!b) return {};
            v.kind = CondValue::Bool;
            v.b = *b;
        } else if (prop->type == "slider") {
            auto n = parseDouble(text);
            if (!n) return {};
            v.kind = CondValue::Number;
            v.n = *n;
        } else {
            v.kind = CondValue::Text;
            v.s = text;
        }
        return v;
    }

    std::string_view text_;
    const std::vector<UserProperty>& properties_;
    const PropertyValues& values_;
    size_t pos_ = 0;
    bool failed_ = false;
};

}  // namespace

bool valueText(const json& v, std::string& out) {
    if (v.is_string()) {
        out = v.get<std::string>();
    } else if (v.is_boolean()) {
        out = v.get<bool>() ? "true" : "false";
    } else if (v.is_number_integer()) {
        out = std::to_string(v.get<long long>());
    } else if (v.is_number()) {
        out = v.dump();
    } else {
        return false;
    }
    return true;
}

std::optional<bool> textToBool(std::string_view text) {
    text = trim(text);
    if (text == "true") return true;
    if (text == "false") return false;
    if (auto n = parseDouble(text)) return *n != 0;
    return std::nullopt;
}

std::optional<double> textToNumber(std::string_view text) {
    text = trim(text);
    if (text == "true") return 1.0;
    if (text == "false") return 0.0;
    return parseDouble(text);
}

int textToFloats(std::string_view text, float* out, int maxCount) {
    int count = 0;
    size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() &&
               (std::isspace(static_cast<unsigned char>(text[pos])) || text[pos] == ','))
            pos++;
        if (pos >= text.size()) break;
        size_t end = pos;
        while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end])) &&
               text[end] != ',')
            end++;
        auto n = parseDouble(text.substr(pos, end - pos));
        if (!n) return -1;
        if (count < maxCount) out[count] = static_cast<float>(*n);
        count++;
        pos = end;
    }
    if (count == 0) return -1;
    return std::min(count, maxCount);
}

std::vector<UserProperty> parseUserProperties(const json& project) {
    std::vector<UserProperty> result;
    if (!project.is_object()) return result;
    const auto general = project.find("general");
    if (general == project.end() || !general->is_object()) return result;
    const auto props = general->find("properties");
    if (props == general->end() || !props->is_object()) return result;

    for (const auto& [name, prop] : props->items()) {
        if (!prop.is_object()) continue;
        UserProperty p;
        p.name = name;
        if (auto type = prop.find("type"); type != prop.end()) {
            if (!type->is_string()) continue;  // malformed, not a label
            p.type = lower(type->get<std::string>());
        }
        if (auto t = prop.find("text"); t != prop.end() && t->is_string())
            p.text = t->get<std::string>();
        if (auto o = prop.find("order"); o != prop.end() && o->is_number())
            p.order = static_cast<int>(o->get<double>());
        if (auto i = prop.find("index"); i != prop.end() && i->is_number())
            p.index = static_cast<int>(i->get<double>());
        if (auto c = prop.find("condition"); c != prop.end() && c->is_string())
            p.condition = c->get<std::string>();
        if (auto v = prop.find("value"); v != prop.end()) valueText(*v, p.value);

        if (p.type == "combo") {
            if (auto options = prop.find("options"); options != prop.end() && options->is_array()) {
                for (const auto& opt : *options) {
                    if (!opt.is_object() || !opt.contains("value")) continue;
                    ComboOption o;
                    if (!valueText(opt["value"], o.value)) continue;
                    if (opt.contains("label") && opt["label"].is_string())
                        o.label = opt["label"].get<std::string>();
                    p.options.push_back(std::move(o));
                }
            }
        } else if (p.type == "slider") {
            p.min = number(prop, "min");
            p.max = number(prop, "max");
            p.step = number(prop, "step");
            p.hasRange = prop.contains("min") && prop.contains("max");
            if (auto pr = prop.find("precision"); pr != prop.end() && pr->is_number())
                p.precision = static_cast<int>(pr->get<double>());
            if (auto f = prop.find("fraction"); f != prop.end() && f->is_boolean())
                p.fraction = f->get<bool>();
        }
        result.push_back(std::move(p));
    }
    std::sort(result.begin(), result.end(), [](const UserProperty& a, const UserProperty& b) {
        return a.order != b.order ? a.order < b.order : a.name < b.name;
    });
    return result;
}

std::vector<UserProperty> readUserProperties(const std::filesystem::path& wallpaperDir) {
    std::ifstream in(wallpaperDir / "project.json", std::ios::binary);
    if (!in) return {};
    json project = json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (project.is_discarded()) return {};
    return parseUserProperties(project);
}

PropertyValues defaultValues(const std::vector<UserProperty>& properties) {
    PropertyValues values;
    for (const auto& p : properties)
        if (!p.type.empty() && p.type != "text" && p.type != "group") values[p.name] = p.value;
    return values;
}

bool evaluateCondition(std::string_view expression, const std::vector<UserProperty>& properties,
                       const PropertyValues& values) {
    if (trim(expression).empty()) return true;
    auto result = ConditionParser(expression, properties, values).run();
    return result.value_or(true);
}

}  // namespace canvas::scene
