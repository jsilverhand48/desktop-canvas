// Implementation of loadScene and applyProperties declared in
// SceneLoader.h.
#include "scene/model/SceneLoader.h"

#include <set>

#include "scene/model/Json.h"
#include "scene/particles/ParticleSystem.h"

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
        if (m && !m->puppet.empty()) puppet(m->puppet);
        return m;
    }

    void puppet(const std::string& path) {
        if (out_.puppets.count(path)) return;
        std::shared_ptr<const MdlFile> f;
        if (auto asset = assets_.find(path)) {
            std::string error;
            auto parsed = std::make_shared<MdlFile>(MdlFile::parse(asset->bytes, error));
            if (error.empty()) f = parsed;
            else {
                out_.report.error(path + ": " + error);
                out_.unreadablePuppets.insert(path);
            }
        } else {
            out_.report.error(path + ": not found");
        }
        out_.puppets[path] = f;
    }

    void cameraPath(const std::string& path) {
        if (path.empty() || out_.cameraPaths.count(path)) return;
        int count = -1;
        if (auto j = json(path); j && j->is_object()) {
            auto it = j->find("paths");
            if (it == j->end()) count = 0;
            else if (it->is_array()) count = int(it->size());
        }
        out_.cameraPaths[path] = count;
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

    // A particle file, its material and its children (each path once).
    std::shared_ptr<ParticleFile> particle(const std::string& path) {
        if (path.empty()) return nullptr;
        if (auto it = out_.particles.find(path); it != out_.particles.end()) return it->second;
        std::shared_ptr<ParticleFile> p;
        if (auto j = json(path)) p = std::make_shared<ParticleFile>(parseParticleFile(*j, out_.report, path));
        out_.particles[path] = p;
        if (p) {
            // Section 4 and 5 of src/scene/particles/README.md: components
            // no phase knows are reported and skipped.
            for (const auto& name : particleNeeds(*p).unknown)
                out_.report.warn(path + ": particle component " + name + " not supported");
            material(p->material);
            for (const auto& child : p->children) particle(child.file);
        }
        return p;
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
        out_.textures[path] =
            TextureSummary{tex->payload, tex->formatCode, tex->has(TextureFile::kSpriteSheet)};
    }

    // Everything one object names (each path once per Loader).
    void object(const SceneObject& object) {
        for (const auto& e : object.effects) {
            effect(e.file);
            for (const auto& pass : e.passes)
                for (const auto& t : pass.textures)
                    if (t) texture(*t);
        }
        if (object.image) {
            model(object.image->model);
            if (object.image->instance)
                for (const auto& t : object.image->instance->textures)
                    if (t) texture(*t);
        }
        if (object.particle) particle(object.particle->particle);
        if (object.camera) cameraPath(object.camera->path);
        if (object.sound)
            for (const auto& s : object.sound->sounds) exists(s);
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

    for (auto& object : out.scene.objects) load.object(object);

    out.values = defaultValues(out.project.properties);
    for (const auto& [name, value] : overrides) out.values[name] = value;
    resolveAll(out, &out.report);
    return true;
}

void loadObject(const AssetLocator& assets, LoadedScene& scene, size_t index) {
    if (index >= scene.scene.objects.size()) return;
    Loader load(assets, scene);
    load.object(scene.scene.objects[index]);
    scene.scene.objects[index].forEachField([&](auto& field) { resolve(field, scene.values, nullptr); });
}

void applyProperties(LoadedScene& scene, const PropertyValues& changes) {
    for (const auto& [name, value] : changes) scene.values[name] = value;
    resolveAll(scene, nullptr);
}

}  // namespace canvas::scene
