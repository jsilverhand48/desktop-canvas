// Implementation of parseProjectFile declared in Project.h.
#include "scene/model/Project.h"

#include <algorithm>
#include <cctype>

namespace canvas::scene {

ProjectFile parseProjectFile(const nlohmann::json& project, LoadReport& report) {
    ProjectFile p;
    if (!project.is_object()) {
        report.warn("project.json: not an object");
        return p;
    }
    if (auto f = project.find("file"); f != project.end()) {
        if (f->is_string() && !f->get<std::string>().empty()) p.file = f->get<std::string>();
        else report.warn("project.json.file: not a path, scene.json assumed");
    }
    if (auto t = project.find("type"); t != project.end() && t->is_string()) {
        p.type = t->get<std::string>();
        std::transform(p.type.begin(), p.type.end(), p.type.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    }
    if (auto w = project.find("workshopid"); w != project.end()) valueText(*w, p.workshopId);
    if (auto g = project.find("general"); g != project.end() && g->is_object())
        if (auto a = g->find("supportsaudioprocessing"); a != g->end() && a->is_boolean())
            p.supportsAudioProcessing = a->get<bool>();
    p.properties = parseUserProperties(project);
    return p;
}

}  // namespace canvas::scene
