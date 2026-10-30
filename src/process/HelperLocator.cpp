// Implementation of HelperLocator declared in HelperLocator.h.
//
// The override is an environment variable named after the helper in upper
// case with dashes as underscores plus "_PATH", for example
// DESKTOP_CANVAS_SCENE_PATH for desktop-canvas-scene.
#include "process/HelperLocator.h"

#include <unistd.h>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace canvas {

namespace fs = std::filesystem;

namespace {

bool executable(const fs::path& candidate) {
    std::error_code ec;
    return fs::is_regular_file(candidate, ec) &&
           access(candidate.c_str(), X_OK) == 0;
}

std::string overrideVariable(const std::string& name) {
    std::string var;
    for (char c : name)
        var += c == '-' ? '_'
                        : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return var + "_PATH";
}

}  // namespace

std::string HelperLocator::find(const std::string& name) {
    if (const char* forced = std::getenv(overrideVariable(name).c_str());
        forced && *forced)
        return executable(forced) ? std::string(forced) : std::string();
    std::error_code ec;
    auto self = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        auto candidate = self.parent_path() / name;
        if (executable(candidate)) return candidate.string();
    }
    if (const char* path = std::getenv("PATH")) {
        std::istringstream stream(path);
        std::string dir;
        while (std::getline(stream, dir, ':')) {
            if (dir.empty()) continue;
            auto candidate = fs::path(dir) / name;
            if (executable(candidate)) return candidate.string();
        }
    }
    return {};
}

}  // namespace canvas
