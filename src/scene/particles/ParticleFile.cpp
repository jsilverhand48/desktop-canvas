// Implementation of parseParticleFile declared in ParticleFile.h.
#include "scene/particles/ParticleFile.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace canvas::scene {

namespace {

using nlohmann::json;

// The first number in a string ("3", "3.5 0 0").
bool leadingNumber(const std::string& s, double& out) {
    const char* begin = s.c_str();
    char* end = nullptr;
    double v = std::strtod(begin, &end);
    if (end == begin || !std::isfinite(v)) return false;
    out = v;
    return true;
}

bool toNumber(const json& v, double& out) {
    if (v.is_number()) {
        out = v.get<double>();
        return std::isfinite(out);
    }
    if (v.is_boolean()) {
        out = v.get<bool>() ? 1 : 0;
        return true;
    }
    if (v.is_string()) return leadingNumber(v.get<std::string>(), out);
    return false;
}

Vec3 toVec3(const json& v, Vec3 fallback) {
    Vec3 out = fallback;
    if (v.is_number()) {
        float f = v.get<float>();
        if (std::isfinite(f)) out = {f, f, f};
    } else if (v.is_string()) {
        const std::string s = v.get<std::string>();
        const char* p = s.c_str();
        for (int i = 0; i < 3; i++) {
            char* end = nullptr;
            double d = std::strtod(p, &end);
            if (end == p) break;
            if (std::isfinite(d)) out[size_t(i)] = float(d);
            p = end;
        }
    } else if (v.is_array()) {
        for (size_t i = 0; i < 3 && i < v.size(); i++)
            if (v[i].is_number()) out[i] = v[i].get<float>();
    }
    return out;
}

const json* field(const json& o, const char* key) {
    auto it = o.find(key);
    if (it == o.end() || it->is_null()) return nullptr;
    return &*it;
}

std::vector<ParticleComponent> components(const json& root, const char* key, LoadReport& report,
                                          const std::string& ctx) {
    std::vector<ParticleComponent> out;
    const json* list = field(root, key);
    if (!list) return out;
    if (!list->is_array()) {
        report.warn(ctx + "." + key + ": not a list");
        return out;
    }
    for (const auto& entry : *list) {
        if (!entry.is_object() || !entry.contains("name") || !entry["name"].is_string()) {
            report.warn(ctx + "." + key + ": entry without a name skipped");
            continue;
        }
        ParticleComponent c;
        c.name = entry["name"].get<std::string>();
        c.params = entry;
        c.params.erase("name");
        double id = 0;
        if (const json* v = field(entry, "id"); v && toNumber(*v, id)) c.id = int64_t(id);
        out.push_back(std::move(c));
    }
    return out;
}

}  // namespace

bool ParticleComponent::has(const char* key) const { return field(params, key) != nullptr; }

double ParticleComponent::number(const char* key, double fallback) const {
    double v = 0;
    if (const json* f = field(params, key); f && toNumber(*f, v)) return v;
    return fallback;
}

Vec3 ParticleComponent::vec3(const char* key, Vec3 fallback) const {
    if (const json* f = field(params, key)) return toVec3(*f, fallback);
    return fallback;
}

int64_t ParticleComponent::integer(const char* key, int64_t fallback) const {
    return int64_t(std::llround(number(key, double(fallback))));
}

std::string ParticleComponent::text(const char* key) const {
    if (const json* f = field(params, key); f && f->is_string()) return f->get<std::string>();
    return {};
}

ParticleFile parseParticleFile(const json& j, LoadReport& report, const std::string& path) {
    ParticleFile p;
    if (!j.is_object()) {
        report.warn(path + ": not an object");
        return p;
    }
    if (const json* v = field(j, "material"); v && v->is_string()) p.material = v->get<std::string>();
    double n = 0;
    if (const json* v = field(j, "maxcount"); v && toNumber(*v, n))
        p.maxCount = std::clamp<int64_t>(int64_t(n), 0, ParticleFile::kMaxParticles);
    if (const json* v = field(j, "starttime"); v && toNumber(*v, n)) p.startTime = std::max(0.0, n);
    if (const json* v = field(j, "flags"); v && toNumber(*v, n)) p.flags = int64_t(n);
    if (const json* v = field(j, "animationmode"); v && v->is_string())
        p.randomFrame = v->get<std::string>() == "randomframe";
    if (p.has(ParticleFile::kRandomFrame)) p.randomFrame = true;
    if (const json* v = field(j, "sequencemultiplier"); v && toNumber(*v, n) && n > 0)
        p.sequenceMultiplier = n;
    p.emitters = components(j, "emitter", report, path);
    p.initializers = components(j, "initializer", report, path);
    p.operators = components(j, "operator", report, path);
    p.renderers = components(j, "renderer", report, path);
    if (const json* cps = field(j, "controlpoint"); cps && cps->is_array()) {
        for (const auto& c : *cps) {
            if (!c.is_object()) continue;
            ParticleControlPoint cp;
            if (const json* v = field(c, "id"); v && toNumber(*v, n)) cp.id = int(n);
            if (cp.id < 0 || cp.id > 7) continue;
            if (const json* v = field(c, "flags"); v && toNumber(*v, n)) cp.flags = int64_t(n);
            if (const json* v = field(c, "offset")) cp.offset = toVec3(*v, cp.offset);
            if (const json* v = field(c, "angles")) cp.angles = toVec3(*v, cp.angles);
            if (const json* v = field(c, "parentcontrolpoint"); v && toNumber(*v, n)) cp.parent = int(n);
            // Flag 1 locks the point to the pointer; the older locktopointer
            // key alone does not (section 8).
            cp.lockToPointer = (cp.flags & 1) != 0;
            p.controlPoints.push_back(cp);
        }
    }
    if (const json* ch = field(j, "children"); ch && ch->is_array()) {
        for (const auto& c : *ch) {
            if (!c.is_object()) continue;
            ParticleChild child;
            if (const json* v = field(c, "name"); v && v->is_string()) child.file = v->get<std::string>();
            if (child.file.empty()) continue;
            if (const json* v = field(c, "type"); v && v->is_string()) child.type = v->get<std::string>();
            if (child.type.empty()) child.type = "static";
            if (const json* v = field(c, "flags"); v && toNumber(*v, n)) child.flags = int64_t(n);
            if (const json* v = field(c, "origin")) child.origin = toVec3(*v, child.origin);
            if (const json* v = field(c, "angles")) child.angles = toVec3(*v, child.angles);
            if (const json* v = field(c, "scale")) child.scale = toVec3(*v, child.scale);
            if (const json* v = field(c, "maxcount"); v && toNumber(*v, n)) child.maxCount = int64_t(n);
            if (const json* v = field(c, "probability"); v && toNumber(*v, n))
                child.probability = std::clamp(n, 0.0, 1.0);
            if (const json* v = field(c, "controlpointstartindex"); v && toNumber(*v, n))
                child.controlPointStart = int64_t(n);
            p.children.push_back(std::move(child));
        }
    }
    return p;
}

}  // namespace canvas::scene
