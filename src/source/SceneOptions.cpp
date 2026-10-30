// See SceneOptions.h for the contract.
#include "source/SceneOptions.h"

#include <algorithm>
#include <cctype>
#include <fstream>

#include <nlohmann/json.hpp>

namespace canvas {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

// Values as text in the format lwe's --set-property parses.
bool scalarToText(const json& v, std::string& out) {
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

double number(const json& obj, const char* key) {
    auto it = obj.find(key);
    return it != obj.end() && it->is_number() ? it->get<double>() : 0.0;
}

}  // namespace

std::vector<UserProperty> readUserProperties(const fs::path& wallpaperDir) {
    std::vector<UserProperty> result;
    std::ifstream in(wallpaperDir / "project.json");
    if (!in) return result;
    json project = json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (project.is_discarded() || !project.is_object()) return result;

    const auto general = project.find("general");
    if (general == project.end() || !general->is_object()) return result;
    const auto props = general->find("properties");
    if (props == general->end() || !props->is_object()) return result;

    for (const auto& [name, prop] : props->items()) {
        if (!prop.is_object()) continue;
        const auto type = prop.find("type");
        if (type == prop.end() || !type->is_string()) continue;

        UserProperty p;
        p.name = name;
        p.type = lower(type->get<std::string>());
        if (prop.contains("text") && prop["text"].is_string())
            p.text = prop["text"].get<std::string>();
        if (prop.contains("order") && prop["order"].is_number_integer())
            p.order = prop["order"].get<int>();
        if (prop.contains("value")) scalarToText(prop["value"], p.value);

        if (p.type == "combo") {
            const auto options = prop.find("options");
            if (options == prop.end() || !options->is_array()) continue;
            for (const auto& opt : *options) {
                if (!opt.is_object() || !opt.contains("value")) continue;
                ComboOption o;
                if (!scalarToText(opt["value"], o.value)) continue;
                if (opt.contains("label") && opt["label"].is_string())
                    o.label = opt["label"].get<std::string>();
                p.options.push_back(std::move(o));
            }
            if (p.options.empty()) continue;
        } else if (p.type == "slider") {
            p.min = number(prop, "min");
            p.max = number(prop, "max");
            p.step = number(prop, "step");
            p.hasRange = prop.contains("min") && prop.contains("max");
        }
        result.push_back(std::move(p));
    }
    std::sort(result.begin(), result.end(),
              [](const UserProperty& a, const UserProperty& b) {
                  return a.order != b.order ? a.order < b.order
                                            : a.name < b.name;
              });
    return result;
}

bool isNsfwOption(const ComboOption& option) {
    for (const std::string& text : {lower(option.label), lower(option.value)})
        if (text.find("nsfw") != std::string::npos ||
            text.find("nude") != std::string::npos)
            return true;
    return false;
}

PropertyList resolveSceneProperties(
    const std::vector<UserProperty>& properties,
    const std::map<std::string, std::string>& pinned,
    const std::map<std::string, std::vector<std::string>>& allowed,
    bool randomize, bool includeNsfw, std::mt19937& rng) {
    std::map<std::string, std::string> out(pinned.begin(), pinned.end());
    if (randomize) {
        for (const auto& prop : properties) {
            if (prop.type != "combo" || pinned.count(prop.name)) continue;
            std::vector<const ComboOption*> eligible;
            auto restrict = allowed.find(prop.name);
            if (restrict != allowed.end() && !restrict->second.empty()) {
                // The user's explicit choice, NSFW included if they chose it.
                for (const auto& opt : prop.options)
                    if (std::find(restrict->second.begin(),
                                  restrict->second.end(),
                                  opt.value) != restrict->second.end())
                        eligible.push_back(&opt);
            } else {
                for (const auto& opt : prop.options)
                    if (includeNsfw || !isNsfwOption(opt))
                        eligible.push_back(&opt);
            }
            // Nothing to vary unless something was filtered out: then even a
            // single remaining option is set, so an excluded default cannot
            // show.
            const bool filtered = eligible.size() != prop.options.size();
            if (eligible.empty() || (eligible.size() < 2 && !filtered))
                continue;
            std::uniform_int_distribution<size_t> pick(0, eligible.size() - 1);
            out[prop.name] = eligible[pick(rng)]->value;
        }
    }
    return PropertyList(out.begin(), out.end());
}

}  // namespace canvas
