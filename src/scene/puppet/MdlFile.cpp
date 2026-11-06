// Implementation of the MDL reader declared in MdlFile.h
// (src/scene/puppet/README.md section 2).
#include "scene/puppet/MdlFile.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

#include <nlohmann/json.hpp>

#include "scene/assets/ByteReader.h"

namespace canvas::scene {

namespace {

constexpr size_t kMaxVertices = 16u << 20;
constexpr uint32_t kMaxBones = 4096;
constexpr uint32_t kMaxAnimations = 4096;

struct VertexLayout {
    uint32_t format;
    uint32_t stride;
    int bones;    // offset of 4 x u32 bone indices, -1: none
    int weights;  // offset of 4 x f32 weights
    int uv;
};

// Section 2.1's table.
constexpr VertexLayout kLayouts[] = {
    {0x9, 20, -1, -1, 12},
    {0x1800009, 52, 12, 28, 44},
    {0x180000F, 80, 40, 56, 72},
    {0x181000E, 84, 44, 60, 76},
};

uint32_t readU32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

float readF32(const uint8_t* p) {
    uint32_t bits = readU32(p);
    float f;
    std::memcpy(&f, &bits, 4);
    return std::isfinite(f) ? f : 0.0f;
}

// The first "NAMEdddd\0" at or after from; npos when absent.
size_t findTag(std::span<const uint8_t> data, size_t from, const char* name) {
    for (size_t i = from; i + 9 <= data.size(); i++) {
        if (std::memcmp(data.data() + i, name, 4) != 0) continue;
        bool digits = true;
        for (size_t k = 4; k < 8; k++) digits = digits && data[i + k] >= '0' && data[i + k] <= '9';
        if (digits && data[i + 8] == 0) return i;
    }
    return std::string::npos;
}

int tagVersion(std::span<const uint8_t> data, size_t at) {
    int v = 0;
    for (size_t k = 4; k < 8; k++) v = v * 10 + (data[at + k] - '0');
    return v;
}

uint32_t u16(ByteReader& r, const char* what) {
    auto b = r.bytes(2, what);
    return b.size() == 2 ? uint32_t(b[0] | b[1] << 8) : 0;
}

// NUL terminated text of at most limit bytes.
std::string cstring(ByteReader& r, const char* what, size_t limit = 1 << 20) {
    std::string out;
    while (r.ok()) {
        auto b = r.bytes(1, what);
        if (b.empty()) break;
        if (b[0] == 0) return out;
        if (out.size() >= limit) {
            r.fail(what, "text too long");
            break;
        }
        out.push_back(char(b[0]));
    }
    return out;
}

Mat4 readMatrix(ByteReader& r, const char* what) {
    Mat4 m;
    for (float& v : m.m) {
        v = r.f32(what);
        if (!std::isfinite(v)) v = 0;
    }
    return m;
}

const char* modeName(PuppetPlayMode m) {
    switch (m) {
        case PuppetPlayMode::Loop: return "loop";
        case PuppetPlayMode::Mirror: return "mirror";
        case PuppetPlayMode::Single: return "single";
    }
    return "loop";
}

bool parseMode(const std::string& s, PuppetPlayMode& mode) {
    for (auto m : {PuppetPlayMode::Loop, PuppetPlayMode::Mirror, PuppetPlayMode::Single})
        if (s == modeName(m)) {
            mode = m;
            return true;
        }
    return false;
}

// One animation header at the reader's position; false when it does not
// look like one (section 2.4's plausibility rule).
bool readAnimationHeader(ByteReader& r, size_t boneCount, PuppetAnimation& a, uint32_t& tracks) {
    a.id = r.i32("animation id");
    if (r.u32("animation field") != 0) return false;  // always 0 [corpus 47/47]
    a.name = cstring(r, "animation name", 255);
    std::string mode = cstring(r, "animation mode", 16);
    a.fps = r.f32("animation fps");
    a.length = r.u32("animation length");
    r.u32("animation field");
    tracks = r.u32("animation bone count");
    return r.ok() && parseMode(mode, a.mode) && a.fps > 0 && a.fps <= 1000 && tracks == boneCount;
}

Vec2 vec2Of(const nlohmann::json& j, Vec2 fallback) {
    if (!j.is_string()) return fallback;
    std::istringstream in(j.get<std::string>());
    float x = 0, y = 0;
    if (in >> x >> y && std::isfinite(x) && std::isfinite(y)) return {x, y};
    return fallback;
}

float zOf(const nlohmann::json& j, float fallback) {
    if (!j.is_string()) return fallback;
    std::istringstream in(j.get<std::string>());
    float x = 0, y = 0, z = 0;
    if (in >> x >> y >> z && std::isfinite(z)) return z;
    return fallback;
}

float numberOf(const nlohmann::json& j, const char* key, float fallback) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_number()) return fallback;
    double v = it->get<double>();
    return std::isfinite(v) ? float(v) : fallback;
}

bool boolOf(const nlohmann::json& j, const char* key, bool fallback) {
    auto it = j.find(key);
    return it != j.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

}  // namespace

BonePhysics parseBonePhysics(const std::string& text) {
    BonePhysics p;
    if (text.empty()) return p;
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false, true);
    if (!j.is_object()) return p;
    p.rotation = boolOf(j, "r", false);
    p.translation = boolOf(j, "t", false);
    p.spring = boolOf(j, "se", true);
    p.rotStiffness = numberOf(j, "rs", p.rotStiffness);
    p.rotFriction = numberOf(j, "rf", p.rotFriction);
    p.rotInertia = numberOf(j, "ri", p.rotInertia);
    p.moveStiffness = numberOf(j, "ts", p.moveStiffness);
    p.moveFriction = numberOf(j, "tf", p.moveFriction);
    p.moveInertia = numberOf(j, "ti", p.moveInertia);
    p.limitAngle = boolOf(j, "la", false);
    if (auto it = j.find("lamin"); it != j.end()) p.minAngle = zOf(*it, p.minAngle);
    if (auto it = j.find("lamax"); it != j.end()) p.maxAngle = zOf(*it, p.maxAngle);
    if (p.minAngle > p.maxAngle) std::swap(p.minAngle, p.maxAngle);
    p.limitDistance = boolOf(j, "lt", false);
    p.maxDistance = std::max(0.0f, numberOf(j, "ltmax", p.maxDistance));
    p.mass = numberOf(j, "m", p.mass);
    if (!(p.mass > 0)) p.mass = 20;
    p.tipSize = std::max(0.0f, numberOf(j, "s", 0));
    if (auto it = j.find("a"); it != j.end()) p.tipDirection = vec2Of(*it, p.tipDirection);
    p.gravity = boolOf(j, "ge", false);
    if (auto it = j.find("gd"); it != j.end()) p.gravityDirection = vec2Of(*it, p.gravityDirection);
    p.ik = boolOf(j, "ik", false);
    return p;
}

MdlFile MdlFile::parse(std::span<const uint8_t> data, std::string& error) {
    MdlFile f;
    error.clear();
    ByteReader r(data);
    std::string tag = r.tag("MDLV tag");
    if (!r.ok() || tag.compare(0, 4, "MDLV") != 0) {
        error = "not an MDL file";
        return f;
    }
    f.version = tagVersion(data, 0);
    uint32_t headerFormat = r.u32("format");
    r.u32("header field");
    r.u32("header field");
    f.material = cstring(r, "material path", 4096);
    if (f.version >= 17) {
        for (int i = 0; i < 7; i++) r.u32("mesh field");
        f.vertexFormat = r.u32("vertex format");
    } else if (f.version >= 16) {
        r.u32("mesh field");
        f.vertexFormat = r.u32("vertex format");
    } else {
        r.u32("mesh field");
        f.vertexFormat = headerFormat;
    }
    const VertexLayout* layout = nullptr;
    for (const auto& l : kLayouts)
        if (l.format == f.vertexFormat) layout = &l;
    if (!r.ok()) {
        error = r.error();
        return f;
    }
    if (!layout) {
        error = "unknown vertex format " + std::to_string(f.vertexFormat);
        return f;
    }
    uint32_t vertexBytes = r.u32("vertex bytes");
    if (vertexBytes % layout->stride || vertexBytes / layout->stride > kMaxVertices) {
        error = "vertex bytes do not fit the format";
        return f;
    }
    auto vertices = r.bytes(vertexBytes, "vertices");
    uint32_t indexBytes = r.u32("index bytes");
    auto indexData = r.bytes(indexBytes, "indices");
    if (!r.ok()) {
        error = r.error();
        return f;
    }
    if (indexBytes % 6) {
        error = "index bytes are not whole triangles";
        return f;
    }
    const size_t count = vertexBytes / layout->stride;
    PuppetMesh& m = f.mesh;
    m.positions.resize(count);
    m.uvs.resize(count);
    if (layout->bones >= 0) {
        m.bones.resize(count);
        m.weights.resize(count);
    }
    for (size_t v = 0; v < count; v++) {
        const uint8_t* p = vertices.data() + v * layout->stride;
        m.positions[v] = {readF32(p), readF32(p + 4), readF32(p + 8)};
        m.uvs[v] = {readF32(p + layout->uv), readF32(p + layout->uv + 4)};
        if (layout->bones >= 0)
            for (size_t k = 0; k < 4; k++) {
                m.bones[v][k] = readU32(p + layout->bones + 4 * k);
                m.weights[v][k] = readF32(p + layout->weights + 4 * k);
            }
    }
    m.indices.resize(indexBytes / 2);
    for (size_t i = 0; i < m.indices.size(); i++) {
        m.indices[i] = uint16_t(indexData[2 * i] | indexData[2 * i + 1] << 8);
        if (m.indices[i] >= count) {
            error = "index past the vertices";
            m = {};
            return f;
        }
    }
    const size_t meshEnd = r.offset();
    f.blendShapes = findTag(data, meshEnd, "MDMP") != std::string::npos;

    // Skeleton (section 2.2).
    const size_t skeletonAt = findTag(data, meshEnd, "MDLS");
    bool skeletonWhole = false;
    if (skeletonAt != std::string::npos) {
        ByteReader s(data.subspan(skeletonAt + 9));
        uint32_t end = s.u32("skeleton end");
        uint32_t n = s.u32("bone count");
        if (n > kMaxBones) s.fail("bone count", "too many bones");
        for (uint32_t b = 0; b < n && s.ok(); b++) {
            PuppetBone bone;
            bone.name = cstring(s, "bone name", 1024);
            s.u32("bone flags");
            int32_t parent = s.i32("bone parent");
            uint32_t size = s.u32("bone matrix size");
            if (!s.ok()) break;
            if (size != 64) {
                s.fail("bone matrix size", "not 64");
                break;
            }
            bone.rest = readMatrix(s, "bone matrix");
            std::string physics = cstring(s, "bone physics");
            if (!s.ok()) break;
            if (parent >= int32_t(b) || parent < -1) {
                s.fail("bone parent", "not an earlier bone");
                break;
            }
            bone.parent = parent;
            const auto& R = bone.rest.m;
            bone.restPosition = {R[12], R[13]};
            bone.restZ = R[14];
            bone.restAngle = std::atan2(R[1], R[0]);
            bone.restScale = {std::hypot(R[0], R[1]), std::hypot(R[4], R[5])};
            bone.physics = parseBonePhysics(physics);
            f.ik = f.ik || bone.physics.ik;
            f.bones.push_back(std::move(bone));
        }
        skeletonWhole = s.ok() && f.bones.size() == n;
        if (!skeletonWhole) f.notes.push_back("skeleton damaged (" + s.error() + "), animations dropped");
        (void)end;
    }

    // Attachments (section 2.3).
    if (const size_t at = findTag(data, meshEnd, "MDAT"); at != std::string::npos && !f.bones.empty()) {
        ByteReader a(data.subspan(at + 9));
        a.u32("attachments end");
        uint32_t n = u16(a, "attachment count");
        for (uint32_t i = 0; i < n && a.ok(); i++) {
            PuppetAttachment att;
            att.bone = int(u16(a, "attachment bone"));
            att.name = cstring(a, "attachment name", 1024);
            att.matrix = readMatrix(a, "attachment matrix");
            if (!a.ok()) break;
            if (att.bone >= int(f.bones.size())) {
                f.notes.push_back("attachment on a missing bone dropped");
                continue;
            }
            f.attachments.push_back(std::move(att));
        }
        if (!a.ok()) f.notes.push_back("attachments damaged: " + a.error());
    }

    // Animations (section 2.4).
    const size_t animAt = findTag(data, meshEnd, "MDLA");
    if (animAt != std::string::npos && skeletonWhole && !f.bones.empty()) {
        const size_t nb = f.bones.size();
        ByteReader a(data.subspan(animAt + 9));
        a.u32("animations end");
        uint32_t n = std::min(a.u32("animation count"), kMaxAnimations);
        const size_t base = animAt + 9;
        size_t pos = a.offset();
        for (uint32_t i = 0; i < n; i++) {
            if (i > 0) {
                // Skip the version specific bytes up to the next header.
                bool found = false;
                for (size_t p = pos; p + 40 <= data.size() - base; p++) {
                    ByteReader probe(data.subspan(base + p));
                    PuppetAnimation tmp;
                    uint32_t tracks = 0;
                    if (readAnimationHeader(probe, nb, tmp, tracks)) {
                        pos = p;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    f.notes.push_back("animation " + std::to_string(i) + " not found");
                    break;
                }
            }
            ByteReader h(data.subspan(base + pos));
            PuppetAnimation anim;
            uint32_t tracks = 0;
            if (!readAnimationHeader(h, nb, anim, tracks)) {
                f.notes.push_back("animation " + std::to_string(i) + " header damaged");
                break;
            }
            bool good = anim.length > 0 && anim.length < (1u << 24);
            if (good) anim.keys.resize(nb * (size_t(anim.length) + 1));
            for (size_t b = 0; b < nb && h.ok(); b++) {
                h.u32("track field");
                uint32_t size = h.u32("track size");
                auto track = h.bytes(size, "track keys");
                if (!h.ok()) break;
                const size_t keys = size / 36;
                for (size_t k = 0; good && k <= anim.length; k++) {
                    PuppetKey& key = anim.keys[b * (size_t(anim.length) + 1) + k];
                    if (k < keys) {
                        const uint8_t* p = track.data() + k * 36;
                        key.position = {readF32(p), readF32(p + 4), readF32(p + 8)};
                        key.angles = {readF32(p + 12), readF32(p + 16), readF32(p + 20)};
                        key.scale = {readF32(p + 24), readF32(p + 28), readF32(p + 32)};
                    } else {
                        key = keys ? anim.keys[b * (size_t(anim.length) + 1) + keys - 1] : PuppetKey{};
                    }
                }
                if (keys == 0) good = false;
            }
            if (!h.ok()) {
                f.notes.push_back("animation " + std::to_string(anim.id) + " truncated");
                break;
            }
            pos += h.offset();
            if (good) f.animations.push_back(std::move(anim));
            else f.notes.push_back("animation " + std::to_string(anim.id) + " dropped");
        }
    }
    return f;
}

int MdlFile::boneIndex(const std::string& name) const {
    for (size_t i = 0; i < bones.size(); i++)
        if (bones[i].name == name) return int(i);
    return -1;
}

const PuppetAnimation* MdlFile::animation(int32_t id) const {
    for (const auto& a : animations)
        if (a.id == id) return &a;
    return nullptr;
}

int MdlFile::attachmentIndex(const std::string& name) const {
    for (size_t i = 0; i < attachments.size(); i++)
        if (attachments[i].name == name) return int(i);
    return -1;
}

}  // namespace canvas::scene
