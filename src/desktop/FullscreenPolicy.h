// Which outputs a fullscreen application hides, given every toplevel window
// the desktop reports (Platform::watchToplevels or the KDE bridge) and the
// user's rules (config "fullscreen_pause").
//
// An output is covered when some window is fullscreen on it, its app id
// does not contain any ignore pattern (case insensitive substring, so
// "steam" also matches "steam_app_570"), and, with onlyActive, it is the
// focused window. Disabled rules cover nothing. Pure function: no state, no
// display access, so tests call it directly. InstanceManager pauses
// instances whose outputs are all covered.
#pragma once

#include <set>
#include <string>
#include <vector>

#include "display/Platform.h"

namespace canvas {

struct FullscreenRules {
    bool enabled = true;
    bool onlyActive = false;
    std::vector<std::string> ignoreAppIds;
};

std::set<std::string> coveredOutputs(const std::vector<ToplevelInfo>& windows,
                                     const FullscreenRules& rules);

}  // namespace canvas
