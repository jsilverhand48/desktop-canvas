// Implementation of the model, material and effect parsers declared in
// Resources.h.
#include "scene/model/Resources.h"

namespace canvas::scene {

using nlohmann::json;

namespace {

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

bool flag(const json& object, const char* key, LoadReport& report, const std::string& ctx) {
    bool out = false;
    if (const json* v = find(object, key); v && !readPlain(*v, out))
        report.warn(ctx + "." + key + ": not a bool");
    return out;
}

std::optional<float> number(const json& object, const char* key) {
    float out = 0;
    if (const json* v = find(object, key); v && readPlain(*v, out)) return out;
    return std::nullopt;
}

std::optional<int64_t> integer(const json& object, const char* key) {
    int64_t out = 0;
    if (const json* v = find(object, key); v && readPlain(*v, out)) return out;
    return std::nullopt;
}

}  // namespace

ModelFile parseModelFile(const json& model, LoadReport& report, const std::string& path) {
    ModelFile m;
    m.material = text(model, "material");
    if (m.material.empty()) report.warn(path + ": no material");
    m.autosize = flag(model, "autosize", report, path);
    m.fullscreen = flag(model, "fullscreen", report, path);
    m.passthrough = flag(model, "passthrough", report, path);
    m.solidLayer = flag(model, "solidlayer", report, path);
    m.noPadding = flag(model, "nopadding", report, path);
    m.instanced = flag(model, "instanced", report, path);
    m.projectLayer = flag(model, "projectlayer", report, path);
    m.width = number(model, "width");
    m.height = number(model, "height");
    if (const json* crop = find(model, "cropoffset")) {
        Vec2 v{0, 0};
        if (readPlain(*crop, v)) m.cropOffset = v;
        else report.warn(path + ".cropoffset: not a vector");
    }
    m.puppet = text(model, "puppet");
    return m;
}

MaterialFile parseMaterialFile(const json& material, LoadReport& report, const std::string& path) {
    MaterialFile file;
    const json* passes = find(material, "passes");
    if (!passes || !passes->is_array()) {
        report.warn(path + ": no passes");
        return file;
    }
    int index = 0;
    for (const auto& p : *passes) {
        MaterialPass pass;
        std::string ctx = path + ".pass " + std::to_string(index++);
        if (!p.is_object()) {
            report.warn(ctx + ": not an object");
            file.passes.push_back(std::move(pass));
            continue;
        }
        pass.shader = text(p, "shader");
        if (pass.shader.empty()) report.warn(ctx + ": no shader");
        pass.blending = text(p, "blending");
        pass.cullMode = text(p, "cullmode");
        pass.depthTest = text(p, "depthtest");
        pass.depthWrite = text(p, "depthwrite");
        pass.alphaWriting = text(p, "alphawriting");
        if (const json* t = find(p, "textures"); t && t->is_array())
            for (const auto& entry : *t) {
                std::string name;
                if (!entry.is_null() && valueText(entry, name)) pass.textures.emplace_back(name);
                else pass.textures.emplace_back(std::nullopt);
            }
        if (const json* c = find(p, "combos"); c && c->is_object())
            for (const auto& [name, value] : c->items()) {
                int64_t n = 0;
                if (readPlain(value, n)) pass.combos[name] = static_cast<int>(n);
                else report.warn(ctx + ".combos." + name + ": not a number");
            }
        if (const json* c = find(p, "constantshadervalues"); c && c->is_object())
            for (const auto& [name, value] : c->items())
                pass.constants[name] =
                    readValue(value, Floats{}, report, ctx + ".constantshadervalues." + name);
        if (const json* u = find(p, "usershadervalues"); u && u->is_object())
            for (const auto& [uniform, property] : u->items()) {
                std::string name;
                if (valueText(property, name)) pass.userShaderValues[uniform] = name;
            }
        if (const json* u = find(p, "usertextures"); u && u->is_array())
            for (const auto& entry : *u) {
                std::string name;
                if (!entry.is_null() && valueText(entry, name)) pass.userTextures.push_back(name);
            }
        file.passes.push_back(std::move(pass));
    }
    return file;
}

EffectFile parseEffectFile(const json& effect, LoadReport& report, const std::string& path) {
    EffectFile file;
    file.name = text(effect, "name");
    file.group = text(effect, "group");
    file.version = integer(effect, "version");
    if (const json* deps = find(effect, "dependencies"); deps && deps->is_array())
        for (const auto& d : *deps)
            if (d.is_string()) file.dependencies.push_back(d.get<std::string>());
    if (const json* fbos = find(effect, "fbos"); fbos && fbos->is_array())
        for (const auto& f : *fbos) {
            if (!f.is_object()) continue;
            EffectFbo fbo;
            fbo.name = text(f, "name");
            fbo.format = text(f, "format");
            fbo.uvs = text(f, "uvs");
            fbo.scale = number(f, "scale").value_or(1.0f);
            fbo.width = integer(f, "width");
            fbo.height = integer(f, "height");
            fbo.unique = flag(f, "unique", report, path + ".fbo " + fbo.name);
            file.fbos.push_back(std::move(fbo));
        }
    const json* passes = find(effect, "passes");
    if (!passes || !passes->is_array()) {
        report.warn(path + ": no passes");
        return file;
    }
    for (const auto& p : *passes) {
        EffectPassDefinition pass;
        if (p.is_object()) {
            pass.material = text(p, "material");
            pass.target = text(p, "target");
            pass.command = text(p, "command");
            pass.source = text(p, "source");
            pass.compose = flag(p, "compose", report, path + ".pass");
            if (const json* bind = find(p, "bind"); bind && bind->is_array())
                for (const auto& b : *bind)
                    if (b.is_object())
                        pass.bind.push_back({text(b, "name"), integer(b, "index").value_or(0)});
        } else {
            report.warn(path + ": pass is not an object");
        }
        file.passes.push_back(std::move(pass));
    }
    return file;
}

}  // namespace canvas::scene
