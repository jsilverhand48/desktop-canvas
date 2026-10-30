// Pure geometry over the output layout (Platform::outputs()), shared by the
// X11 toplevel tracker, the KDE bridge's global cursor and screen spans.
// Header only and free of display libraries, so tests use it directly.
//
// Coordinates are global logical pixels. An output covers the half open
// rectangle [x, x + width) x [y, y + height).
#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "display/Platform.h"

namespace canvas::geometry {

// The output containing the point, or nullptr.
inline const OutputDesc* outputAt(const std::vector<OutputDesc>& outputs,
                                  double x, double y) {
    for (const auto& o : outputs) {
        if (x >= o.x && x < o.x + o.width && y >= o.y && y < o.y + o.height)
            return &o;
    }
    return nullptr;
}

// Outputs a window rectangle covers at least half of: a fullscreen window
// lands on exactly its own output, one spanning two monitors on both, and a
// window that only overlaps an edge on neither.
inline std::vector<std::string> outputsCoveredBy(
    const std::vector<OutputDesc>& outputs, int x, int y, int width,
    int height) {
    std::vector<std::string> result;
    for (const auto& o : outputs) {
        long long left = std::max(x, o.x);
        long long top = std::max(y, o.y);
        long long right = std::min(x + width, o.x + o.width);
        long long bottom = std::min(y + height, o.y + o.height);
        if (right <= left || bottom <= top) continue;
        long long overlap = (right - left) * (bottom - top);
        long long area = static_cast<long long>(o.width) * o.height;
        if (area > 0 && overlap * 2 >= area) result.push_back(o.name);
    }
    return result;
}

}  // namespace canvas::geometry
