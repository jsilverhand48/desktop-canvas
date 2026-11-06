// Implementation of parseSceneFile declared in SceneFile.h.
#include "scene/model/SceneFile.h"

#include <cctype>

namespace canvas::scene {

using nlohmann::json;

const char* objectKindName(ObjectKind kind) {
    switch (kind) {
        case ObjectKind::Image: return "image";
        case ObjectKind::Particle: return "particle";
        case ObjectKind::Text: return "text";
        case ObjectKind::Sound: return "sound";
        case ObjectKind::Light: return "light";
        case ObjectKind::Camera: return "camera";
        case ObjectKind::Shape: return "shape";
        case ObjectKind::Model: return "model";
        case ObjectKind::Group: return "group";
    }
    return "?";
}

namespace {

// Re-reads a field keeping the default its struct declared.
template <class T>
void read(Field<T>& field, const json& object, const char* key, LoadReport& report,
          const std::string& context) {
    field = readField(object, key, field.stored, report, context);
}

const json* find(const json& object, const char* key) {
    if (!object.is_object()) return nullptr;
    auto it = object.find(key);
    return it != object.end() && !it->is_null() ? &*it : nullptr;
}

std::string text(const json& object, const char* key) {
    std::string out;
    if (const json* v = find(object, key)) valueText(*v, out);
    return out;
}

std::optional<int64_t> integer(const json& object, const char* key) {
    int64_t out = 0;
    if (const json* v = find(object, key); v && readPlain(*v, out)) return out;
    return std::nullopt;
}

std::map<std::string, int> combos(const json& object, LoadReport& report,
                                  const std::string& context) {
    std::map<std::string, int> out;
    const json* c = find(object, "combos");
    if (!c) return out;
    if (!c->is_object()) {
        report.warn(context + ".combos: not an object");
        return out;
    }
    for (const auto& [name, value] : c->items()) {
        int64_t n = 0;
        if (readPlain(value, n)) out[name] = static_cast<int>(n);
        else report.warn(context + ".combos." + name + ": not a number");
    }
    return out;
}

std::vector<std::optional<std::string>> textureList(const json& object, LoadReport& report,
                                                     const std::string& context) {
    std::vector<std::optional<std::string>> out;
    const json* t = find(object, "textures");
    if (!t) return out;
    if (!t->is_array()) {
        report.warn(context + ".textures: not a list");
        return out;
    }
    for (const auto& entry : *t) {
        std::string name;
        if (!entry.is_null() && valueText(entry, name)) out.emplace_back(name);
        else out.emplace_back(std::nullopt);
    }
    return out;
}

EffectInstance parseEffect(const json& e, LoadReport& report, const std::string& context) {
    EffectInstance effect;
    effect.file = text(e, "file");
    effect.id = integer(e, "id").value_or(0);
    effect.name = text(e, "name");
    effect.username = text(e, "username");
    std::string ctx = context + ".effect " + std::to_string(effect.id);
    read(effect.visible, e, "visible", report, ctx);
    if (const json* passes = find(e, "passes"); passes && passes->is_array()) {
        for (const auto& p : *passes) {
            EffectPassSettings pass;
            if (!p.is_object()) {
                report.warn(ctx + ": pass is not an object");
                effect.passes.push_back(std::move(pass));  // keep positions
                continue;
            }
            pass.id = integer(p, "id").value_or(0);
            pass.combos = combos(p, report, ctx);
            pass.textures = textureList(p, report, ctx);
            if (const json* c = find(p, "constantshadervalues"); c && c->is_object())
                for (const auto& [name, value] : c->items())
                    pass.constants[name] = readValue(value, Floats{}, report,
                                                     ctx + ".constantshadervalues." + name);
            effect.passes.push_back(std::move(pass));
        }
    }
    return effect;
}

AnimationLayer parseAnimationLayer(const json& a, LoadReport& report, const std::string& context) {
    AnimationLayer layer;
    layer.id = integer(a, "id").value_or(0);
    layer.name = text(a, "name");
    layer.animation = integer(a, "animation").value_or(0);
    std::string ctx = context + ".animationlayer " + std::to_string(layer.id);
    read(layer.rate, a, "rate", report, ctx);
    read(layer.blend, a, "blend", report, ctx);
    read(layer.blendTime, a, "blendtime", report, ctx);
    read(layer.visible, a, "visible", report, ctx);
    read(layer.additive, a, "additive", report, ctx);
    read(layer.blendIn, a, "blendin", report, ctx);
    read(layer.blendOut, a, "blendout", report, ctx);
    return layer;
}

ObjectKind kindOf(const json& o) {
    // The first kind key with a non null value decides: particles carry
    // "image": null (section 5).
    if (find(o, "image")) return ObjectKind::Image;
    if (find(o, "particle")) return ObjectKind::Particle;
    if (find(o, "text")) return ObjectKind::Text;
    if (find(o, "sound")) return ObjectKind::Sound;
    if (find(o, "light")) return ObjectKind::Light;
    if (find(o, "camera")) return ObjectKind::Camera;
    if (find(o, "shape")) return ObjectKind::Shape;
    if (find(o, "model")) return ObjectKind::Model;
    return ObjectKind::Group;
}

bool controlPointKey(const std::string& key) {
    for (const char* prefix : {"controlpointangle", "controlpoint"}) {
        std::string p(prefix);
        if (key.size() > p.size() && key.compare(0, p.size(), p) == 0) {
            for (size_t i = p.size(); i < key.size(); i++)
                if (!std::isdigit(static_cast<unsigned char>(key[i]))) return false;
            return true;
        }
    }
    return false;
}

void parseKind(SceneObject& obj, const json& o, LoadReport& report, const std::string& ctx) {
    switch (obj.kind) {
        case ObjectKind::Image: {
            ImageLayer& image = obj.image.emplace();
            if (const json* m = find(o, "image"); m && m->is_string()) image.model = m->get<std::string>();
            else report.warn(ctx + ".image: not a path");
            read(image.size, o, "size", report, ctx);
            read(image.alpha, o, "alpha", report, ctx);
            read(image.brightness, o, "brightness", report, ctx);
            read(image.colorBlendMode, o, "colorBlendMode", report, ctx);
            read(image.color, o, "color", report, ctx);
            read(image.alignment, o, "alignment", report, ctx);
            read(image.copyBackground, o, "copybackground", report, ctx);
            read(image.ledSource, o, "ledsource", report, ctx);
            read(image.clampUVs, o, "clampuvs", report, ctx);
            read(image.noInterpolation, o, "nointerpolation", report, ctx);
            if (const json* config = find(o, "config"))
                read(image.passthrough, *config, "passthrough", report, ctx + ".config");
            if (const json* layers = find(o, "animationlayers"); layers && layers->is_array())
                for (const auto& a : *layers)
                    if (a.is_object()) image.animationLayers.push_back(parseAnimationLayer(a, report, ctx));
            if (const json* inst = find(o, "instance"); inst && inst->is_object()) {
                InstanceMaterial& m = image.instance.emplace();
                m.id = integer(*inst, "id").value_or(0);
                m.combos = combos(*inst, report, ctx + ".instance");
                m.textures = textureList(*inst, report, ctx + ".instance");
                if (const json* ut = find(*inst, "usertextures"); ut && ut->is_array())
                    for (const auto& u : *ut)
                        if (u.is_object() && u.contains("name"))
                            m.userTextures.push_back(text(u, "name"));
            }
            break;
        }
        case ObjectKind::Particle: {
            ParticleLayer& particle = obj.particle.emplace();
            if (const json* p = find(o, "particle"); p && p->is_string())
                particle.particle = p->get<std::string>();
            else report.warn(ctx + ".particle: not a path");
            if (const json* io = find(o, "instanceoverride"); io && io->is_object()) {
                ParticleOverride& ov = particle.overrides.emplace();
                std::string c = ctx + ".instanceoverride";
                ov.id = integer(*io, "id");
                read(ov.alpha, *io, "alpha", report, c);
                read(ov.brightness, *io, "brightness", report, c);
                read(ov.count, *io, "count", report, c);
                read(ov.lifetime, *io, "lifetime", report, c);
                read(ov.rate, *io, "rate", report, c);
                read(ov.size, *io, "size", report, c);
                read(ov.speed, *io, "speed", report, c);
                read(ov.color, *io, "color", report, c);
                read(ov.colorn, *io, "colorn", report, c);
                for (const auto& [key, value] : io->items())
                    if (controlPointKey(key))
                        ov.controlPoints[key] = readValue(value, Vec3{0, 0, 0}, report, c + "." + key);
            }
            break;
        }
        case ObjectKind::Text: {
            TextLayer& t = obj.text.emplace();
            read(t.text, o, "text", report, ctx);
            t.font = text(o, "font");
            read(t.pointSize, o, "pointsize", report, ctx);
            read(t.alpha, o, "alpha", report, ctx);
            read(t.brightness, o, "brightness", report, ctx);
            read(t.backgroundBrightness, o, "backgroundbrightness", report, ctx);
            read(t.maxRows, o, "maxrows", report, ctx);
            read(t.maxWidth, o, "maxwidth", report, ctx);
            read(t.color, o, "color", report, ctx);
            read(t.backgroundColor, o, "backgroundcolor", report, ctx);
            read(t.horizontalAlign, o, "horizontalalign", report, ctx);
            read(t.verticalAlign, o, "verticalalign", report, ctx);
            read(t.anchor, o, "anchor", report, ctx);
            read(t.padding, o, "padding", report, ctx);  // number or "x y" [req A1]
            read(t.spacing, o, "spacing", report, ctx);
            read(t.size, o, "size", report, ctx);
            read(t.limitRows, o, "limitrows", report, ctx);
            read(t.limitWidth, o, "limitwidth", report, ctx);
            read(t.limitUseEllipsis, o, "limituseellipsis", report, ctx);
            read(t.blockAlign, o, "blockalign", report, ctx);
            read(t.opaqueBackground, o, "opaquebackground", report, ctx);
            break;
        }
        case ObjectKind::Sound: {
            SoundLayer& s = obj.sound.emplace();
            if (const json* list = find(o, "sound")) {
                if (list->is_string()) s.sounds.push_back(list->get<std::string>());
                else if (list->is_array())
                    for (const auto& entry : *list)
                        if (entry.is_string()) s.sounds.push_back(entry.get<std::string>());
            }
            read(s.playbackMode, o, "playbackmode", report, ctx);
            read(s.volume, o, "volume", report, ctx);
            read(s.minTime, o, "mintime", report, ctx);
            read(s.maxTime, o, "maxtime", report, ctx);
            read(s.startSilent, o, "startsilent", report, ctx);
            read(s.muteInEditor, o, "muteineditor", report, ctx);
            break;
        }
        case ObjectKind::Light: {
            LightLayer& l = obj.light.emplace();
            read(l.light, o, "light", report, ctx);
            read(l.color, o, "color", report, ctx);
            read(l.intensity, o, "intensity", report, ctx);
            read(l.radius, o, "radius", report, ctx);
            read(l.exponent, o, "exponent", report, ctx);
            read(l.density, o, "density", report, ctx);
            read(l.volumetricsExponent, o, "volumetricsexponent", report, ctx);
            read(l.cascade0, o, "cascadedistance0", report, ctx);
            read(l.cascade1, o, "cascadedistance1", report, ctx);
            read(l.cascade2, o, "cascadedistance2", report, ctx);
            read(l.innerCone, o, "innercone", report, ctx);
            read(l.outerCone, o, "outercone", report, ctx);
            read(l.controlPoint, o, "controlpoint", report, ctx);
            break;
        }
        case ObjectKind::Camera: {
            CameraPathLayer& c = obj.camera.emplace();
            c.camera = text(o, "camera");
            c.path = text(o, "path");
            c.queueMode = text(o, "queuemode");
            read(c.fov, o, "fov", report, ctx);
            read(c.zoom, o, "zoom", report, ctx);
            break;
        }
        case ObjectKind::Shape:
            obj.shape = text(o, "shape");
            break;
        case ObjectKind::Model:
        case ObjectKind::Group:
            break;
    }
}

SceneObject parseObject(const json& o, LoadReport& report) {
    SceneObject obj;
    obj.source = o;
    obj.id = integer(o, "id").value_or(0);
    obj.name = text(o, "name");
    obj.kind = kindOf(o);
    std::string ctx = "object " + std::to_string(obj.id) + " (" + objectKindName(obj.kind) + ")";
    obj.parent = integer(o, "parent");
    read(obj.origin, o, "origin", report, ctx);
    read(obj.scale, o, "scale", report, ctx);
    read(obj.angles, o, "angles", report, ctx);
    read(obj.visible, o, "visible", report, ctx);
    read(obj.parallaxDepth, o, "parallaxDepth", report, ctx);
    if (const json* a = find(o, "attachment"); a && a->is_string())  // [req A1]
        obj.attachment = a->get<std::string>();
    if (const json* deps = find(o, "dependencies"); deps && deps->is_array())
        for (const auto& d : *deps) {
            int64_t id = 0;
            if (readPlain(d, id)) obj.dependencies.push_back(id);
        }
    read(obj.lockTransforms, o, "locktransforms", report, ctx);
    read(obj.disablePropagation, o, "disablepropagation", report, ctx);
    read(obj.solid, o, "solid", report, ctx);
    read(obj.castShadow, o, "castshadow", report, ctx);
    read(obj.perspective, o, "perspective", report, ctx);
    if (const json* effects = find(o, "effects")) {
        if (effects->is_array()) {
            for (const auto& e : *effects)
                if (e.is_object()) obj.effects.push_back(parseEffect(e, report, ctx));
                else report.warn(ctx + ".effects: entry is not an object");
        } else {
            report.warn(ctx + ".effects: not a list");
        }
    }
    parseKind(obj, o, report, ctx);
    return obj;
}

}  // namespace

SceneObject parseSceneObject(const json& object, LoadReport& report) { return parseObject(object, report); }

SceneFile parseSceneFile(const json& scene, LoadReport& report) {
    SceneFile file;
    if (!scene.is_object()) {
        report.warn("scene: not an object");
        return file;
    }
    if (const json* camera = find(scene, "camera")) {
        read(file.camera.center, *camera, "center", report, "scene.camera");
        read(file.camera.eye, *camera, "eye", report, "scene.camera");
        read(file.camera.up, *camera, "up", report, "scene.camera");
    }
    if (const json* g = find(scene, "general")) {
        General& gen = file.general;
        const std::string c = "scene.general";
        read(gen.ambientColor, *g, "ambientcolor", report, c);
        read(gen.skylightColor, *g, "skylightcolor", report, c);
        read(gen.clearColor, *g, "clearcolor", report, c);
        read(gen.bloomTint, *g, "bloomtint", report, c);
        read(gen.gravityDirection, *g, "gravitydirection", report, c);
        read(gen.windDirection, *g, "winddirection", report, c);
        read(gen.clearEnabled, *g, "clearenabled", report, c);
        read(gen.bloom, *g, "bloom", report, c);
        read(gen.hdr, *g, "hdr", report, c);
        read(gen.cameraFade, *g, "camerafade", report, c);
        read(gen.cameraPreview, *g, "camerapreview", report, c);
        read(gen.cameraParallax, *g, "cameraparallax", report, c);
        read(gen.cameraShake, *g, "camerashake", report, c);
        read(gen.windEnabled, *g, "windenabled", report, c);
        read(gen.bloomStrength, *g, "bloomstrength", report, c);
        read(gen.bloomThreshold, *g, "bloomthreshold", report, c);
        read(gen.bloomHdrFeather, *g, "bloomhdrfeather", report, c);
        read(gen.bloomHdrIterations, *g, "bloomhdriterations", report, c);
        read(gen.bloomHdrScatter, *g, "bloomhdrscatter", report, c);
        read(gen.bloomHdrStrength, *g, "bloomhdrstrength", report, c);
        read(gen.bloomHdrThreshold, *g, "bloomhdrthreshold", report, c);
        read(gen.zoom, *g, "zoom", report, c);
        read(gen.fov, *g, "fov", report, c);
        read(gen.nearZ, *g, "nearz", report, c);
        read(gen.farZ, *g, "farz", report, c);
        read(gen.perspectiveOverrideFov, *g, "perspectiveoverridefov", report, c);
        read(gen.cameraParallaxAmount, *g, "cameraparallaxamount", report, c);
        read(gen.cameraParallaxDelay, *g, "cameraparallaxdelay", report, c);
        read(gen.cameraParallaxMouseInfluence, *g, "cameraparallaxmouseinfluence", report, c);
        read(gen.cameraShakeAmplitude, *g, "camerashakeamplitude", report, c);
        read(gen.cameraShakeRoughness, *g, "camerashakeroughness", report, c);
        read(gen.cameraShakeSpeed, *g, "camerashakespeed", report, c);
        read(gen.gravityStrength, *g, "gravitystrength", report, c);
        read(gen.windStrength, *g, "windstrength", report, c);
        if (const json* light = find(*g, "lightconfig")) {
            read(gen.lightPoint, *light, "point", report, c + ".lightconfig");
            read(gen.lightPointShadow, *light, "pointshadow", report, c + ".lightconfig");
        }
        if (const json* p = find(*g, "orthogonalprojection")) {
            read(gen.projectionWidth, *p, "width", report, c + ".orthogonalprojection");
            read(gen.projectionHeight, *p, "height", report, c + ".orthogonalprojection");
        }
    }
    if (const json* objects = find(scene, "objects")) {
        if (objects->is_array()) {
            for (const auto& o : *objects)
                if (o.is_object()) file.objects.push_back(parseObject(o, report));
                else report.warn("scene.objects: entry is not an object, skipped");
        } else {
            report.warn("scene.objects: not a list");
        }
    }
    file.version = integer(scene, "version");
    return file;
}

LightKind lightKind(const std::string& name) {
    if (name == "lpoint" || name == "point") return LightKind::Point;
    if (name == "lspot" || name == "spot") return LightKind::Spot;
    if (name == "ltube" || name == "tube") return LightKind::Tube;
    if (name == "ldirectional" || name == "directional") return LightKind::Directional;
    return LightKind::Unknown;
}

}  // namespace canvas::scene
