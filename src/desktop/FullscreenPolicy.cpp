// Implementation of coveredOutputs declared in FullscreenPolicy.h.
#include "desktop/FullscreenPolicy.h"

#include <algorithm>
#include <cctype>

namespace canvas {

namespace {

std::string lower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool ignored(const std::string& appId, const std::vector<std::string>& patterns) {
    std::string id = lower(appId);
    return std::any_of(patterns.begin(), patterns.end(), [&](const std::string& p) {
        return !p.empty() && id.find(lower(p)) != std::string::npos;
    });
}

}  // namespace

std::set<std::string> coveredOutputs(const std::vector<ToplevelInfo>& windows,
                                     const FullscreenRules& rules) {
    std::set<std::string> covered;
    if (!rules.enabled) return covered;
    for (const auto& w : windows) {
        if (!w.fullscreen) continue;
        if (rules.onlyActive && !w.active) continue;
        if (ignored(w.appId, rules.ignoreAppIds)) continue;
        covered.insert(w.outputs.begin(), w.outputs.end());
    }
    return covered;
}

}  // namespace canvas
