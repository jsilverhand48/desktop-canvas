// The files scene objects name, per src/scene/model/README.md sections 6
// to 8: models (models/*.json), materials and effects. Like scene.json
// they parse tolerantly into structs with presence flags; SceneLoader
// loads each path once and shares the result between objects.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "scene/model/Field.h"
#include "scene/model/LoadReport.h"

namespace canvas::scene {

struct ModelFile {
    std::string material;
    bool autosize = false, fullscreen = false, passthrough = false, solidLayer = false,
         noPadding = false, instanced = false, projectLayer = false;
    std::optional<float> width, height;
    std::optional<Vec2> cropOffset;
    std::string puppet;  // MDL path, phase P8
};

struct MaterialPass {
    std::string shader;  // shaders/<shader>.frag and .vert
    std::string blending, cullMode, depthTest, depthWrite, alphaWriting;  // empty: absent
    std::vector<std::optional<std::string>> textures;
    std::map<std::string, int> combos;
    std::map<std::string, Field<Floats>> constants;
    std::map<std::string, std::string> userShaderValues;  // uniform to property
    std::vector<std::string> userTextures;                 // property names
    template <class F> void forEachField(F&& f) {
        for (auto& [name, c] : constants) f(c);
    }
};

struct MaterialFile {
    std::vector<MaterialPass> passes;
    template <class F> void forEachField(F&& f) {
        for (auto& p : passes) p.forEachField(f);
    }
};

struct EffectFbo {
    std::string name, format, uvs;
    float scale = 1.0f;
    std::optional<int64_t> width, height;
    bool unique = false;
};

struct EffectBind {
    std::string name;
    int64_t index = 0;
};

struct EffectPassDefinition {
    std::string material, target, command, source;
    bool compose = false;
    std::vector<EffectBind> bind;
};

struct EffectFile {
    std::string name, group;
    std::optional<int64_t> version;
    std::vector<std::string> dependencies;
    std::vector<EffectFbo> fbos;
    std::vector<EffectPassDefinition> passes;
};

ModelFile parseModelFile(const nlohmann::json& model, LoadReport& report, const std::string& path);
MaterialFile parseMaterialFile(const nlohmann::json& material, LoadReport& report,
                               const std::string& path);
EffectFile parseEffectFile(const nlohmann::json& effect, LoadReport& report, const std::string& path);

}  // namespace canvas::scene
