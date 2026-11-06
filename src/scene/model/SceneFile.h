// scene.json, per src/scene/model/README.md sections 4 and 5: camera,
// general settings and objects with their effect instances and animation
// layers.
//
// parseSceneFile() never fails on content: values of the wrong type keep
// their defaults (warnings in the report), malformed objects are skipped
// with a warning. Absent settings stay `present == false`; transform and
// appearance fields default to the identity (section 5). Paths are kept
// as written; SceneLoader resolves them.
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

enum class ObjectKind { Image, Particle, Text, Sound, Light, Camera, Shape, Model, Group };
const char* objectKindName(ObjectKind kind);

struct Camera {
    Field<Vec3> center{Vec3{0, 0, 0}};
    Field<Vec3> eye{Vec3{0, 0, 1}};
    Field<Vec3> up{Vec3{0, 1, 0}};
    template <class F> void forEachField(F&& f) { f(center); f(eye); f(up); }
};

struct General {
    Field<Vec3> ambientColor, skylightColor, clearColor, bloomTint, gravityDirection,
        windDirection;
    Field<bool> clearEnabled, bloom, hdr, cameraFade, cameraPreview, cameraParallax,
        cameraShake, windEnabled;
    Field<float> bloomStrength, bloomThreshold, bloomHdrFeather, bloomHdrIterations,
        bloomHdrScatter, bloomHdrStrength, bloomHdrThreshold, zoom, fov, nearZ, farZ,
        perspectiveOverrideFov, cameraParallaxAmount, cameraParallaxDelay,
        cameraParallaxMouseInfluence, cameraShakeAmplitude, cameraShakeRoughness,
        cameraShakeSpeed, gravityStrength, windStrength, lightPoint, lightPointShadow;
    Field<float> projectionWidth, projectionHeight;  // orthogonalprojection

    template <class F> void forEachField(F&& f) {
        for (auto* v : {&ambientColor, &skylightColor, &clearColor, &bloomTint,
                        &gravityDirection, &windDirection})
            f(*v);
        for (auto* v : {&clearEnabled, &bloom, &hdr, &cameraFade, &cameraPreview,
                        &cameraParallax, &cameraShake, &windEnabled})
            f(*v);
        for (auto* v : {&bloomStrength, &bloomThreshold, &bloomHdrFeather, &bloomHdrIterations,
                        &bloomHdrScatter, &bloomHdrStrength, &bloomHdrThreshold, &zoom, &fov,
                        &nearZ, &farZ, &perspectiveOverrideFov, &cameraParallaxAmount,
                        &cameraParallaxDelay, &cameraParallaxMouseInfluence,
                        &cameraShakeAmplitude, &cameraShakeRoughness, &cameraShakeSpeed,
                        &gravityStrength, &windStrength, &lightPoint, &lightPointShadow,
                        &projectionWidth, &projectionHeight})
            f(*v);
    }
};

// Per pass settings of an effect instance (section 5.1); passes match the
// effect file's passes by position.
struct EffectPassSettings {
    int64_t id = 0;
    std::map<std::string, int> combos;
    std::map<std::string, Field<Floats>> constants;  // by material name
    std::vector<std::optional<std::string>> textures;  // null keeps the effect's
    template <class F> void forEachField(F&& f) {
        for (auto& [name, c] : constants) f(c);
    }
};

struct EffectInstance {
    std::string file;
    int64_t id = 0;
    std::string name;
    std::string username;
    Field<bool> visible{true};
    std::vector<EffectPassSettings> passes;
    template <class F> void forEachField(F&& f) {
        f(visible);
        for (auto& p : passes) p.forEachField(f);
    }
};

// Puppet animation layer (section 5.2, req A1).
struct AnimationLayer {
    int64_t id = 0;
    std::string name;
    int64_t animation = 0;
    Field<float> rate{1.0f}, blend{1.0f}, blendTime{0.0f};
    Field<bool> visible{false}, additive{false}, blendIn{false}, blendOut{false};
    template <class F> void forEachField(F&& f) {
        f(rate); f(blend); f(blendTime); f(visible); f(additive); f(blendIn); f(blendOut);
    }
};

// Per instance material of instanced solid layers.
struct InstanceMaterial {
    int64_t id = 0;
    std::map<std::string, int> combos;
    std::vector<std::optional<std::string>> textures;
    std::vector<std::string> userTextures;  // names, e.g. "$mediaThumbnail"
};

struct ImageLayer {
    std::string model;  // models/*.json
    Field<Vec2> size{Vec2{0, 0}};
    Field<float> alpha{1.0f}, brightness{1.0f}, colorBlendMode{0.0f};
    Field<Vec3> color{Vec3{1, 1, 1}};
    Field<std::string> alignment{std::string("center")};
    Field<bool> copyBackground, ledSource, clampUVs, noInterpolation, passthrough;
    std::vector<AnimationLayer> animationLayers;
    std::optional<InstanceMaterial> instance;
    template <class F> void forEachField(F&& f) {
        f(size); f(alpha); f(brightness); f(colorBlendMode); f(color); f(alignment);
        for (auto* b : {&copyBackground, &ledSource, &clampUVs, &noInterpolation, &passthrough})
            f(*b);
        for (auto& a : animationLayers) a.forEachField(f);
    }
};

struct ParticleOverride {
    std::optional<int64_t> id;
    Field<float> alpha{1.0f}, brightness{1.0f}, count{1.0f}, lifetime{1.0f}, rate{1.0f},
        size{1.0f}, speed{1.0f};
    Field<Vec3> color{Vec3{1, 1, 1}}, colorn{Vec3{1, 1, 1}};
    std::map<std::string, Field<Vec3>> controlPoints;  // controlpointN, controlpointangleN
    template <class F> void forEachField(F&& f) {
        for (auto* v : {&alpha, &brightness, &count, &lifetime, &rate, &size, &speed}) f(*v);
        f(color); f(colorn);
        for (auto& [name, c] : controlPoints) f(c);
    }
};

struct ParticleLayer {
    std::string particle;  // particles/*.json (phase P5)
    std::optional<ParticleOverride> overrides;
    template <class F> void forEachField(F&& f) {
        if (overrides) overrides->forEachField(f);
    }
};

struct TextLayer {
    Field<std::string> text;
    std::string font;
    Field<float> pointSize, alpha{1.0f}, brightness{1.0f}, backgroundBrightness, maxRows,
        maxWidth;
    Field<Vec3> color{Vec3{1, 1, 1}}, backgroundColor{Vec3{0, 0, 0}};
    Field<std::string> horizontalAlign{std::string("center")},
        verticalAlign{std::string("center")}, anchor{std::string("none")};
    Field<Vec2> padding{Vec2{0, 0}}, spacing{Vec2{0, 0}}, size{Vec2{0, 0}};
    Field<bool> limitRows, limitWidth, limitUseEllipsis, blockAlign, opaqueBackground;
    template <class F> void forEachField(F&& f) {
        f(text);
        for (auto* v : {&pointSize, &alpha, &brightness, &backgroundBrightness, &maxRows,
                        &maxWidth})
            f(*v);
        f(color); f(backgroundColor); f(horizontalAlign); f(verticalAlign); f(anchor);
        f(padding); f(spacing); f(size);
        for (auto* b : {&limitRows, &limitWidth, &limitUseEllipsis, &blockAlign,
                        &opaqueBackground})
            f(*b);
    }
};

struct SoundLayer {
    std::vector<std::string> sounds;
    Field<std::string> playbackMode{std::string("loop")};
    Field<float> volume{1.0f}, minTime, maxTime;
    Field<bool> startSilent, muteInEditor;
    template <class F> void forEachField(F&& f) {
        f(playbackMode); f(volume); f(minTime); f(maxTime); f(startSilent); f(muteInEditor);
    }
};

// Light layers (src/scene/render/README.md section 12).
struct LightLayer {
    Field<std::string> light;
    Field<Vec3> color{Vec3{1, 1, 1}}, controlPoint{Vec3{0, 0, 0}};
    Field<float> intensity{1.0f}, radius, exponent, density, volumetricsExponent, cascade0,
        cascade1, cascade2, innerCone, outerCone;
    template <class F> void forEachField(F&& f) {
        f(light); f(color); f(controlPoint);
        for (auto* v : {&intensity, &radius, &exponent, &density, &volumetricsExponent,
                        &cascade0, &cascade1, &cascade2, &innerCone, &outerCone})
            f(*v);
    }
};

enum class LightKind { Point, Spot, Tube, Directional, Unknown };
// lpoint and point, lspot, ltube, ldirectional.
LightKind lightKind(const std::string& name);

struct CameraPathLayer {
    std::string camera;
    std::string path;
    std::string queueMode;
    Field<float> fov, zoom;
    template <class F> void forEachField(F&& f) { f(fov); f(zoom); }
};

struct SceneObject {
    int64_t id = 0;
    std::string name;
    ObjectKind kind = ObjectKind::Group;
    std::optional<int64_t> parent;
    Field<Vec3> origin{Vec3{0, 0, 0}}, scale{Vec3{1, 1, 1}}, angles{Vec3{0, 0, 0}};
    Field<bool> visible{true};
    Field<Vec2> parallaxDepth{Vec2{0, 0}};
    std::string attachment;
    std::vector<int64_t> dependencies;
    Field<bool> lockTransforms, disablePropagation, solid, castShadow, perspective;
    std::vector<EffectInstance> effects;
    std::string shape;  // shape layers: "quad"

    // The part matching kind is set.
    std::optional<ImageLayer> image;
    std::optional<ParticleLayer> particle;
    std::optional<TextLayer> text;
    std::optional<SoundLayer> sound;
    std::optional<LightLayer> light;
    std::optional<CameraPathLayer> camera;

    template <class F> void forEachField(F&& f) {
        f(origin); f(scale); f(angles); f(visible); f(parallaxDepth);
        for (auto* b : {&lockTransforms, &disablePropagation, &solid, &castShadow, &perspective})
            f(*b);
        for (auto& e : effects) e.forEachField(f);
        if (image) image->forEachField(f);
        if (particle) particle->forEachField(f);
        if (text) text->forEachField(f);
        if (sound) sound->forEachField(f);
        if (light) light->forEachField(f);
        if (camera) camera->forEachField(f);
    }
};

struct SceneFile {
    Camera camera;
    General general;
    std::vector<SceneObject> objects;  // drawing order
    std::optional<int64_t> version;
    template <class F> void forEachField(F&& f) {
        camera.forEachField(f);
        general.forEachField(f);
        for (auto& o : objects) o.forEachField(f);
    }
};

SceneFile parseSceneFile(const nlohmann::json& scene, LoadReport& report);

}  // namespace canvas::scene
