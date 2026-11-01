// Implementation of collectPrograms and shaderLoader declared in
// Programs.h.
#include "scene/shader/Programs.h"

#include <set>

#include "scene/particles/ParticleSystem.h"

namespace canvas::scene {

namespace {

void apply(ProgramRequest& request, const MaterialPass& pass, const LoadedScene& scene,
           const std::map<std::string, int>* instanceCombos,
           const std::vector<std::optional<std::string>>* instanceTextures) {
    request.shader = pass.shader;
    for (const auto& [name, value] : pass.combos) request.combos[name] = value;
    if (instanceCombos)
        for (const auto& [name, value] : *instanceCombos) request.combos[name] = value;
    for (size_t i = 0; i < size_t(kMaxTextureSlots); i++) {
        const std::optional<std::string>* name = nullptr;
        if (i < pass.textures.size() && pass.textures[i]) name = &pass.textures[i];
        if (instanceTextures && i < instanceTextures->size() && (*instanceTextures)[i])
            name = &(*instanceTextures)[i];
        if (!name) continue;
        request.textureSet[i] = true;
        auto texture = scene.textures.find(texturePath(**name));
        if (texture != scene.textures.end())
            request.textureFormat[i] = static_cast<int>(texture->second.formatCode);
    }
}

}  // namespace

ProgramRequest particleRequest(const ParticleFile& file, const LoadedScene& scene) {
    ProgramRequest request;
    auto material = scene.materials.find(file.material);
    if (material == scene.materials.end() || !material->second || material->second->passes.empty())
        return request;
    const MaterialPass& pass = material->second->passes.front();
    apply(request, pass, scene, nullptr, nullptr);
    // One and two channel albedos need their format (ConvertTexture0Format).
    if (request.textureFormat[0] >= 0) request.combos["TEX0FORMAT"] = request.textureFormat[0];
    const std::string albedo =
        pass.textures.empty() || !pass.textures[0] ? std::string("util/white") : *pass.textures[0];
    auto texture = scene.textures.find(texturePath(albedo));
    if (texture != scene.textures.end() && texture->second.spriteSheet) {
        request.combos["SPRITESHEET"] = 1;
        request.combos["THICKFORMAT"] = 1;
        if (!file.randomFrame && !file.has(ParticleFile::kNoFrameBlending))
            request.combos["SPRITESHEETBLEND"] = 1;
    }
    if (pass.blending == "alphatocoverage") request.combos["ALPHATOCOVERAGE"] = 1;
    // The renderer (section 6.1): trails stretch sprites; ropes swap in
    // Wallpaper Engine's rope shader.
    using Kind = ParticleRendering::Kind;
    const Kind kind = particleRendering(file).kind;
    if (kind == Kind::Rope || kind == Kind::RopeTrail) request.shader = "genericropeparticle";
    if (kind != Kind::Sprite) request.combos["THICKFORMAT"] = 1;
    if (kind == Kind::SpriteTrail || kind == Kind::RopeTrail) request.combos["TRAILRENDERER"] = 1;
    return request;
}

ProgramRequest programRequest(const MaterialPass& pass, const LoadedScene& scene,
                              const std::map<std::string, int>* instanceCombos,
                              const std::vector<std::optional<std::string>>* instanceTextures) {
    ProgramRequest request;
    apply(request, pass, scene, instanceCombos, instanceTextures);
    return request;
}

std::string PassProgram::where() const {
    std::string s = "object " + std::to_string(object);
    if (effect >= 0) s += " effect " + std::to_string(effect);
    return s + " pass " + std::to_string(pass);
}

std::vector<PassProgram> collectPrograms(const LoadedScene& scene) {
    std::vector<PassProgram> out;
    for (const auto& object : scene.scene.objects) {
        if (object.image) {
            auto model = scene.models.find(object.image->model);
            if (model != scene.models.end() && model->second) {
                auto material = scene.materials.find(model->second->material);
                if (material != scene.materials.end() && material->second) {
                    const InstanceMaterial* instance =
                        object.image->instance ? &*object.image->instance : nullptr;
                    for (size_t p = 0; p < material->second->passes.size(); p++) {
                        PassProgram program;
                        program.object = object.id;
                        program.pass = static_cast<int>(p);
                        program.material = model->second->material;
                        apply(program.request, material->second->passes[p], scene,
                              instance ? &instance->combos : nullptr,
                              instance ? &instance->textures : nullptr);
                        int blendMode = int(object.image->colorBlendMode.value);
                        if (blendMode != 0) program.request.combos["BLENDMODE"] = blendMode;
                        if (!program.request.shader.empty()) out.push_back(std::move(program));
                    }
                }
            }
        }
        if (object.particle) {
            // The layer's particle file and every child file it reaches
            // (src/scene/particles/README.md section 9), each once.
            auto find = [&](const std::string& path) -> const ParticleFile* {
                auto it = scene.particles.find(path);
                return it != scene.particles.end() ? it->second.get() : nullptr;
            };
            std::vector<std::pair<const ParticleFile*, int>> todo;
            if (const ParticleFile* f = find(object.particle->particle)) todo.push_back({f, 0});
            std::set<const ParticleFile*> seen;
            while (!todo.empty()) {
                auto [file, depth] = todo.back();
                todo.pop_back();
                if (!seen.insert(file).second) continue;
                PassProgram program;
                program.object = object.id;
                program.pass = int(seen.size()) - 1;
                program.material = file->material;
                program.request = particleRequest(*file, scene);
                if (!program.request.shader.empty()) out.push_back(std::move(program));
                if (depth < 4)
                    for (const auto& child : file->children)
                        if (const ParticleFile* f = find(child.file)) todo.push_back({f, depth + 1});
            }
        }
        for (size_t e = 0; e < object.effects.size(); e++) {
            const EffectInstance& instance = object.effects[e];
            auto effect = scene.effects.find(instance.file);
            if (effect == scene.effects.end() || !effect->second) continue;
            for (size_t p = 0; p < effect->second->passes.size(); p++) {
                const EffectPassDefinition& definition = effect->second->passes[p];
                if (definition.material.empty()) continue;
                auto material = scene.materials.find(definition.material);
                if (material == scene.materials.end() || !material->second ||
                    material->second->passes.empty())
                    continue;
                const EffectPassSettings* settings =
                    p < instance.passes.size() ? &instance.passes[p] : nullptr;
                PassProgram program;
                program.object = object.id;
                program.effect = static_cast<int>(e);
                program.pass = static_cast<int>(p);
                program.material = definition.material;
                apply(program.request, material->second->passes.front(), scene,
                      settings ? &settings->combos : nullptr,
                      settings ? &settings->textures : nullptr);
                if (!program.request.shader.empty()) out.push_back(std::move(program));
            }
        }
    }
    return out;
}

ShaderLoader shaderLoader(const AssetLocator& assets) {
    return [&assets](const std::string& path) -> std::optional<std::string> {
        auto asset = assets.find(path);
        if (!asset) return std::nullopt;
        return std::string(asset->bytes.begin(), asset->bytes.end());
    };
}

}  // namespace canvas::scene
