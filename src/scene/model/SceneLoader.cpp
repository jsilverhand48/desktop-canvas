// Implementation of loadScene and applyProperties declared in
// SceneLoader.h.
#include "scene/model/SceneLoader.h"

#include <set>

#include "scene/model/Json.h"

namespace canvas::scene {

namespace {

class Loader {
public:
    Loader(const AssetLocator& assets, LoadedScene& out) : assets_(assets), out_(out) {}

    std::optional<nlohmann::json> json(const std::string& path) {
        auto asset = assets_.find(path);
        if (!asset) {
            out_.report.error(path + ": not found");
            return std::nullopt;
        }
        std::string error;
        auto parsed = parseJson(asset->bytes, error);
        if (!parsed) out_.report.error(path + ": " + error);
        return parsed;
    }

    std::shared_ptr<MaterialFile> material(const std::string& path) {
        if (path.empty()) return nullptr;
        if (auto it = out_.materials.find(path); it != out_.materials.end()) return it->second;
        std::shared_ptr<MaterialFile> m;
        if (auto j = json(path)) m = std::make_shared<MaterialFile>(parseMaterialFile(*j, out_.report, path));
        out_.materials[path] = m;  // null remembers a failure
        if (m)
            for (const auto& pass : m->passes)
                for (const auto& t : pass.textures)
                    if (t) texture(*t);
        return m;
    }

    std::shared_ptr<ModelFile> model(const std::string& path) {
        if (path.empty()) return nullptr;
        if (auto it = out_.models.find(path); it != out_.models.end()) return it->second;
        std::shared_ptr<ModelFile> m;
        if (auto j = json(path)) m = std::make_shared<ModelFile>(parseModelFile(*j, out_.report, path));
        out_.models[path] = m;
        if (m) material(m->material);
        return m;
    }

    std::shared_ptr<EffectFile> effect(const std::string& path) {
        if (path.empty()) return nullptr;
        if (auto it = out_.effects.find(path); it != out_.effects.end()) return it->second;
        std::shared_ptr<EffectFile> e;
        if (auto j = json(path)) e = std::make_shared<EffectFile>(parseEffectFile(*j, out_.report, path));
        out_.effects[path] = e;
        if (e)
            for (const auto& pass : e->passes) material(pass.material);
        return e;
    }

    void texture(const std::string& name) {
        std::string path = texturePath(name);
        if (path.empty() || !checked_.insert(path).second) return;
        auto asset = assets_.find(path);
        if (!asset) {
            out_.report.error(path + ": not found");
            return;
        }
        std::string error;
        auto tex = parseTexture(asset->bytes, error);
        if (!tex) {
            out_.report.error(path + ": " + error);
            return;
        }
        out_.textures[path] = TextureSummary{tex->payload, tex->formatCode};
    }

    void exists(const std::string& path) {
        if (path.empty() || !checked_.insert(path).second) return;
        if (!assets_.find(path)) out_.report.error(path + ": not found");
    }

private:
    const AssetLocator& assets_;
    LoadedScene& out_;
    std::set<std::string> checked_;
};

void resolveAll(LoadedScene& scene, LoadReport* report) {
    scene.forEachField([&](auto& field) { resolve(field, scene.values, report); });
}

}  // namespace

std::string texturePath(const std::string& name) {
    if (name.empty() || name.rfind("_rt_", 0) == 0 || name.front() == '$') return {};
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".tex") == 0) return name;
    return "materials/" + name + ".tex";
}

bool loadScene(const AssetLocator& assets, const PropertyValues& overrides, LoadedScene& out) {
    out = LoadedScene{};
    Loader load(assets, out);
    auto project = load.json("project.json");
    if (!project) return false;
    out.project = parseProjectFile(*project, out.report);
    auto scene = load.json(out.project.file);
    if (!scene) return false;
    out.scene = parseSceneFile(*scene, out.report);

    for (auto& object : out.scene.objects) {
        for (const auto& e : object.effects) {
            load.effect(e.file);
            for (const auto& pass : e.passes)
                for (const auto& t : pass.textures)
                    if (t) load.texture(*t);
        }
        if (object.image) {
            load.model(object.image->model);
            if (object.image->instance)
                for (const auto& t : object.image->instance->textures)
                    if (t) load.texture(*t);
        }
        if (object.particle) load.exists(object.particle->particle);
        if (object.sound)
            for (const auto& s : object.sound->sounds) load.exists(s);
    }

    out.values = defaultValues(out.project.properties);
    for (const auto& [name, value] : overrides) out.values[name] = value;
    resolveAll(out, &out.report);
    return true;
}

void applyProperties(LoadedScene& scene, const PropertyValues& changes) {
    for (const auto& [name, value] : changes) scene.values[name] = value;
    resolveAll(scene, nullptr);
}

}  // namespace canvas::scene
