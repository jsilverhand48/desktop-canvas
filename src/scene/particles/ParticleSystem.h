// Particle simulation, per src/scene/particles/README.md sections 2 to 5
// (phase P5a: emitters, the initializers and operators that need no
// control points; control points and children are phase P5b).
//
// A ParticleSystem runs one particle file. step(dt) emits, initializes,
// updates and ages particles in that order; prewarm() runs the file's
// start time. Everything random comes from the system's own generator,
// seeded at construction, and noise is this project's own value noise,
// so the same seed and steps give the same particles. Positions are in
// the system's space, or in scene space for worldspace systems (spawn
// points pass through setSpawnTransform()). No GPU.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "scene/particles/ParticleFile.h"
#include "scene/render/Layout.h"

namespace canvas::scene {

struct Particle {
    Vec3 position{0, 0, 0}, velocity{0, 0, 0};
    Vec3 rotation{0, 0, 0}, angularVelocity{0, 0, 0};
    float size = 20, baseSize = 20;
    Vec3 color{1, 1, 1}, baseColor{1, 1, 1};
    float alpha = 1, baseAlpha = 1;
    float age = 0, lifetime = 1;
    float frame = 0;     // sprite sheet frame value (section 6)
    uint32_t seed = 0;   // per particle randoms of the operators
    Vec3 waveOffset{0, 0, 0};  // oscillateposition's current offset
};

// The layer's override factors (section 2), already filtered by the
// system's "disable override" flags.
struct ParticleFactors {
    float count = 1, rate = 1, speed = 1, size = 1, lifetime = 1, alpha = 1;
    Vec3 color{1, 1, 1};
};

class ParticleSystem {
public:
    ParticleSystem(const ParticleFile& file, uint64_t seed);
    ~ParticleSystem();
    ParticleSystem(const ParticleSystem&) = delete;
    ParticleSystem& operator=(const ParticleSystem&) = delete;

    void setFactors(const ParticleFactors& factors) { factors_ = factors; }
    // System space to scene space (worldspace systems spawn through it).
    void setSpawnTransform(const Mat4& world) { spawn_ = world; }
    // dt seconds of scene time; the rate factor applies inside.
    void step(double dt);
    void prewarm();

    const std::vector<Particle>& particles() const { return particles_; }
    const ParticleFile& file() const { return file_; }
    double time() const { return time_; }
    bool worldSpace() const { return file_.has(ParticleFile::kWorldSpace); }

    struct Emitter;
    struct Initializer;
    struct Operator;

private:
    void spawn(Emitter& emitter);

    const ParticleFile& file_;
    uint64_t rng_;
    uint32_t noiseSeed_;  // seeds the noise fields; derived from the system seed
    ParticleFactors factors_;
    Mat4 spawn_ = Mat4::identity();
    double time_ = 0;
    std::vector<Particle> particles_;
    std::vector<std::unique_ptr<Emitter>> emitters_;
    std::vector<std::unique_ptr<Initializer>> initializers_;
    std::vector<std::unique_ptr<Operator>> operators_;
};

// What a particle file uses beyond phase P5a (section 7): components that
// work with control points (and emitters around a point other than 0),
// children, trail and rope renderers or orientations other than screen.
// `unknown` names components no phase handles (reported and skipped).
struct ParticleNeeds {
    bool controlPoints = false, children = false, trails = false;
    std::vector<std::string> unknown;
};
ParticleNeeds particleNeeds(const ParticleFile& file);

// Smooth value noise in -1..1 (section 5), deterministic for a seed.
float valueNoise(float x, float y, float z, uint32_t seed);

}  // namespace canvas::scene
