// The shader programs a loaded scene uses, per src/scene/shader/README.md
// section 4: one request per image layer material pass and per effect
// pass, with combos merged (material pass, then the scene.json instance)
// and texture slots marked set (material textures, overridden by the
// instance's non null entries) with their format codes. Effect pass
// settings match the effect file's passes by position; passes without a
// material (commands such as copy) have no program. Particle and text
// layer materials join in their phases (P5, P6b).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "scene/assets/AssetLocator.h"
#include "scene/model/SceneLoader.h"
#include "scene/shader/Translator.h"

namespace canvas::scene {

struct PassProgram {
    int64_t object = 0;
    int effect = -1;  // index in the object's effects, -1 for its own material
    int pass = 0;
    std::string material;  // material file path
    ProgramRequest request;
    std::string where() const;  // "object 12 effect 0 pass 1", for reports
};

std::vector<PassProgram> collectPrograms(const LoadedScene& scene);

// Shader sources through a wallpaper's lookup layers.
ShaderLoader shaderLoader(const AssetLocator& assets);

}  // namespace canvas::scene
