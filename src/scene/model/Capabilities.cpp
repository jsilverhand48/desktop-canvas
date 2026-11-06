// Implementation of the capability report declared in Capabilities.h.
#include "scene/model/Capabilities.h"

#include "scene/particles/ParticleSystem.h"
#include "scene/script/ScriptEngine.h"

namespace canvas::scene {

std::set<std::string> sceneFeatures(LoadedScene& scene) {
    std::set<std::string> f = {"scene"};
    for (const auto& object : scene.scene.objects) {
        switch (object.kind) {
            case ObjectKind::Image: f.insert("image"); break;
            case ObjectKind::Particle: f.insert("particles"); break;
            case ObjectKind::Text: f.insert("text"); break;
            case ObjectKind::Sound: f.insert("sound"); break;
            case ObjectKind::Light: f.insert("light"); break;
            case ObjectKind::Camera: f.insert("camera-path"); break;
            case ObjectKind::Shape: f.insert("shape-layer"); break;
            case ObjectKind::Model: f.insert("3d-model"); break;
            case ObjectKind::Group: f.insert("group"); break;
        }
        if (!object.effects.empty()) f.insert("effects");
        if (!object.image) continue;
        auto model = scene.models.find(object.image->model);
        if (model == scene.models.end() || !model->second) continue;
        const ModelFile& m = *model->second;
        if (m.solidLayer) f.insert("solid-layer");
        if (m.fullscreen) f.insert("fullscreen-layer");
        if (!m.puppet.empty()) f.insert("puppet");
        bool compose = m.passthrough;
        if (auto mat = scene.materials.find(m.material);
            mat != scene.materials.end() && mat->second && !mat->second->passes.empty())
            compose |= mat->second->passes.front().shader == "composelayer";
        if (compose) f.insert("composition-layer");
    }
    scene.forEachField([&](auto& field) {
        if (field.scripted()) f.insert("script");
        // Scripts that make layers at run time (src/scene/script/README.md
        // section 3): a feature of its own, not drawn yet.
        if (field.scripted() && field.dynamics->script.find("createLayer") != std::string::npos)
            f.insert("script-layers");
        if (field.animated()) f.insert("timeline");
        if (field.bound()) f.insert("user-properties");
    });
    const General& g = scene.scene.general;
    if (g.cameraParallax.value) f.insert("parallax");
    if (g.cameraShake.value) f.insert("camera-shake");
    if (g.bloom.value) f.insert("bloom");
    if (g.hdr.value) f.insert("hdr");
    for (const auto& [path, file] : scene.particles) {
        if (!file) continue;
        const ParticleNeeds needs = particleNeeds(*file);
        if (needs.controlPoints) f.insert("particle-controlpoints");
        if (needs.children) f.insert("particle-children");
        if (needs.trails) f.insert("particle-trails");
    }
    for (const auto& [path, texture] : scene.textures) {
        if (texture.payload == PayloadKind::Video) f.insert("video-texture");
        if (texture.spriteSheet) f.insert("sprite-animation");
    }
    return f;
}

const std::set<std::string>& supportedFeatures() {
    // What phases P4, P5 and P6 draw (src/scene/render/README.md,
    // src/scene/particles/README.md, src/scene/text/README.md,
    // src/scene/script/README.md). hdr as users without Ultra
    // post-processing see it; video-texture needs libmpv, which the helper
    // checks (SceneCheck), as does sound (P7b); script needs QuickJS in
    // the build.
    static const std::set<std::string> supported = [] {
        std::set<std::string> s = {
            "scene",       "image",           "effects",          "solid-layer",
            "fullscreen-layer", "composition-layer", "group",     "user-properties",
            "bloom",       "hdr",             "timeline",         "parallax",
            "camera-shake", "sprite-animation", "video-texture", "particles",
            "particle-controlpoints", "particle-children", "particle-trails", "text", "sound"};
        if (ScriptEngine::available()) s.insert("script");
        return s;
    }();
    return supported;
}

std::vector<std::string> unsupportedOf(const std::set<std::string>& features) {
    std::vector<std::string> out;
    for (const auto& name : features)
        if (!supportedFeatures().count(name)) out.push_back(name);
    return out;
}

nlohmann::json checkReport(const std::string& wallpaper, bool loaded,
                           const std::set<std::string>& features, const LoadReport& report) {
    std::vector<std::string> unsupported =
        loaded ? unsupportedOf(features) : std::vector<std::string>{"unreadable"};
    return {
        {"wallpaper", wallpaper},
        {"supported", unsupported.empty()},
        {"features", features},
        {"unsupported", unsupported},
        {"errors", report.errors},
        {"warnings", report.warnings},
        {"dropped_warnings", report.droppedWarnings},
    };
}

}  // namespace canvas::scene
