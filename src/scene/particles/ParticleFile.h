// The particle file, per src/scene/particles/README.md section 1.
//
// parseParticleFile() reads the system settings (material, maxcount,
// starttime, flags, sprite sheet mode), the component lists (emitters,
// initializers, operators, renderers, each kept with its JSON parameters
// for the simulation to read through the typed getters), control points
// and children. Like the other scene files it never fails on content:
// values of the wrong type keep their defaults and malformed entries are
// skipped, with warnings in the report.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "scene/model/Field.h"
#include "scene/model/LoadReport.h"

namespace canvas::scene {

// One emitter, initializer, operator or renderer: `name` picks the kind,
// `params` holds every other key of the entry.
struct ParticleComponent {
    std::string name;
    int64_t id = 0;
    nlohmann::json params = nlohmann::json::object();

    bool has(const char* key) const;
    // Numbers (a string's first number, a bool as 0/1); fallback when
    // absent, null or unreadable.
    double number(const char* key, double fallback) const;
    // "x y z" strings, arrays or a single number (all three); missing
    // components keep the fallback's.
    Vec3 vec3(const char* key, Vec3 fallback) const;
    int64_t integer(const char* key, int64_t fallback) const;
    std::string text(const char* key) const;
};

struct ParticleControlPoint {
    int id = 0;
    int64_t flags = 0;
    Vec3 offset{0, 0, 0};
    bool lockToPointer = false;  // flag 1 (the `locktopointer` key is not read)
    int parent = -1;             // `parentcontrolpoint`
    Vec3 angles{0, 0, 0};
};

struct ParticleChild {
    std::string file;  // `name`: another particle file
    std::string type;  // static (also when absent), eventfollow, eventspawn, eventdeath
    int64_t flags = 0;
    Vec3 origin{0, 0, 0}, angles{0, 0, 0}, scale{1, 1, 1};
    int64_t maxCount = 0;
    double probability = 1;
    int64_t controlPointStart = -1;
};

struct ParticleFile {
    // System flags (section 1).
    static constexpr int64_t kWorldSpace = 1;
    static constexpr int64_t kRandomFrame = 2;
    static constexpr int64_t kNoFrameBlending = 4;
    static constexpr int64_t kNoColorOverride = 8;
    static constexpr int64_t kNoCountOverride = 16;
    static constexpr int64_t kNoLifetimeOverride = 32;
    static constexpr int64_t kNoSizeOverride = 64;
    static constexpr int64_t kNoSpeedOverride = 128;
    static constexpr int64_t kMaxParticles = 100000;  // this project's cap

    std::string material;
    int64_t maxCount = 0;
    double startTime = 0;
    int64_t flags = 0;
    bool randomFrame = false;      // `animationmode` randomframe or flag 2
    double sequenceMultiplier = 1;
    std::vector<ParticleComponent> emitters, initializers, operators, renderers;
    std::vector<ParticleControlPoint> controlPoints;
    std::vector<ParticleChild> children;

    bool has(int64_t flag) const { return (flags & flag) != 0; }
};

ParticleFile parseParticleFile(const nlohmann::json& json, LoadReport& report, const std::string& path);

}  // namespace canvas::scene
