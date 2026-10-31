// Loading one scene through an AssetLocator, per src/scene/model/README.md.
//
// loadScene() reads project.json and the scene file (both required),
// then every model, material and effect file the objects and effects
// name, each path once (objects share the parsed result), and checks that
// referenced particle, sound and texture files exist (texture headers are
// parsed to spot video textures). Missing or broken referenced files are
// errors in the report but never stop loading. Bound fields are resolved
// against the property values: declared defaults plus `overrides` (for
// example --set-property). applyProperties() merges live changes and
// resolves again.
#pragma once

#include <map>
#include <memory>
#include <string>

#include "scene/assets/AssetLocator.h"
#include "scene/assets/TextureFile.h"
#include "scene/model/LoadReport.h"
#include "scene/model/Project.h"
#include "scene/model/Resources.h"
#include "scene/model/SceneFile.h"
#include "scene/model/UserProperties.h"

namespace canvas::scene {

// A referenced texture's header facts (the pixels load later).
struct TextureSummary {
    PayloadKind payload = PayloadKind::Raw;
    uint32_t formatCode = 0;  // .tex format code (also TEX<n>FORMAT)
    bool spriteSheet = false;  // animated: a frames section follows
};

struct LoadedScene {
    ProjectFile project;
    SceneFile scene;
    std::map<std::string, std::shared_ptr<ModelFile>> models;        // by path
    std::map<std::string, std::shared_ptr<MaterialFile>> materials;  // by path
    std::map<std::string, std::shared_ptr<EffectFile>> effects;      // by path
    std::map<std::string, TextureSummary> textures;  // referenced .tex files found
    PropertyValues values;  // current property values
    LoadReport report;

    template <class F> void forEachField(F&& f) {
        scene.forEachField(f);
        for (auto& [path, m] : materials)
            if (m) m->forEachField(f);  // null: the file failed to load
    }
};

// False (errors in out.report) when project.json or the scene file cannot
// be read; out is filled as far as loading got.
bool loadScene(const AssetLocator& assets, const PropertyValues& overrides, LoadedScene& out);

// Merges changes into scene.values and resolves every bound field again.
void applyProperties(LoadedScene& scene, const PropertyValues& changes);

// The file a material's texture name refers to: "materials/<name>.tex", or
// the name itself when it already ends in .tex. Empty for render targets
// (_rt_...) and system textures ($...).
std::string texturePath(const std::string& name);

}  // namespace canvas::scene
