// Finds the project's helper executables (desktop-canvas-web,
// desktop-canvas-scene).
//
// Search order: next to the running daemon binary (so a build tree and an
// install prefix both use their own helpers), then every PATH entry. An
// explicit override wins when set (tests, or a helper built elsewhere).
#pragma once

#include <string>

namespace canvas {

class HelperLocator {
public:
    // Absolute path of the named helper, or empty when it is not found.
    static std::string find(const std::string& name);
};

}  // namespace canvas
