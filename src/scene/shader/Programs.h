// The shader programs a loaded scene uses, per src/scene/shader/README.md
// section 4: one request per image layer material pass and per effect
// pass, with combos merged (material pass, then the scene.json instance)
// and texture slots marked set (material textures, overridden by the
// instance's non null entries) with their format codes. Effect pass
// settings match the effect file's passes by position; passes without a
// material (commands such as copy) have no program. An image layer's
// colorBlendMode becomes the BLENDMODE combo of its own passes
// (src/scene/render/README.md section 2). A particle layer adds one
// request per particle file it draws, its own and its children's
// (particleRequest; pass numbers count the files); text layer materials
// join in phase P6b.
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

// The request for one material pass with optional scene.json overrides
// (instance combos win over the material's; non null instance textures
// win over the material's); what collectPrograms uses per pass.
ProgramRequest programRequest(const MaterialPass& pass, const LoadedScene& scene,
                              const std::map<std::string, int>* instanceCombos,
                              const std::vector<std::optional<std::string>>* instanceTextures);

// The request for a particle file's material, per section 6 of
// src/scene/particles/README.md: TEX0FORMAT from the albedo's format; when
// the albedo is a sprite sheet SPRITESHEET, THICKFORMAT and, unless frames
// are random or flag 4 turns blending off, SPRITESHEETBLEND;
// ALPHATOCOVERAGE for that blending; for the renderer (section 6.1)
// TRAILRENDERER and THICKFORMAT for sprite trails, genericropeparticle
// with THICKFORMAT for ropes and with TRAILRENDERER too for rope trails.
// An empty shader when the material did not load. Padded sprite sheets'
// SPRITESHEETBLENDNPOT is the renderer's (it knows the uploaded size).
ProgramRequest particleRequest(const ParticleFile& file, const LoadedScene& scene);

// Shader sources through a wallpaper's lookup layers.
ShaderLoader shaderLoader(const AssetLocator& assets);

}  // namespace canvas::scene
