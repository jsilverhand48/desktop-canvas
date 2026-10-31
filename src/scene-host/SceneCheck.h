// Support checks of desktop-canvas-scene, per src/scene/model/README.md
// section 9.
//
// examine() loads one wallpaper's scene model (no GPU) and decides what of
// it this build cannot render; it backs --check, the --strict start used by
// the "auto" engine, and checkAll(). checkAll() examines every scene folder
// in a workshop content root (non scene items are skipped) and prints a
// summary, or with json a JSON array of the --check objects plus each
// folder name as "id". Returns a helper exit code (0, or 2 when root is not
// a folder).
#pragma once

#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "scene/model/UserProperties.h"

namespace canvas::scenehost {

struct Examination {
    bool loaded = false;  // project.json and the scene file were readable
    std::set<std::string> features;
    std::vector<std::string> unsupported;
    nlohmann::json report;  // the --check object
};

// assetsDir empty: guessed from the wallpaper's Steam library.
Examination examine(const std::filesystem::path& wallpaper, const std::filesystem::path& assetsDir,
                    const scene::PropertyValues& overrides);

int checkAll(const std::filesystem::path& root, const std::filesystem::path& assetsDir,
             bool json);

}  // namespace canvas::scenehost
