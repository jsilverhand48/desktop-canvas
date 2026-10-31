// See SceneOptions.h for the contract.
#include "source/SceneOptions.h"

#include <algorithm>
#include <cctype>

namespace canvas {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

}  // namespace

std::vector<UserProperty> readUserProperties(const fs::path& wallpaperDir) {
    std::vector<UserProperty> result = scene::readUserProperties(wallpaperDir);
    std::erase_if(result, [](const UserProperty& p) {
        return p.type.empty() || (p.type == "combo" && p.options.empty());
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
