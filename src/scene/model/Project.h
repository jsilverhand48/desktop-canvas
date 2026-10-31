// project.json for the scene engine, per src/scene/model/README.md
// section 3: the scene file, the type, the workshop id and the user
// properties (parsed by the shared UserProperties code).
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "scene/model/LoadReport.h"
#include "scene/model/UserProperties.h"

namespace canvas::scene {

struct ProjectFile {
    std::string file = "scene.json";
    std::string type;        // lower cased
    std::string workshopId;  // decimal text when given as a number
    bool supportsAudioProcessing = false;
    std::vector<UserProperty> properties;  // labels included
};

ProjectFile parseProjectFile(const nlohmann::json& project, LoadReport& report);

}  // namespace canvas::scene
