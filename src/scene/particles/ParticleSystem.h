// Particle simulation, per src/scene/particles/README.md sections 2 to 9:
// emitters, initializers and operators (phase P5a), control points, the
// control point components, children and rope trail history (phase P5b).
//
// A ParticleSystem runs one particle file. step(dt) places the control
// points, then emits, initializes, updates and ages particles in that
// order, keeps rope trail positions and runs the child systems; prewarm()
// runs the file's start time. Everything random comes from the system's
// own generator, seeded at construction, and noise is this project's own
// value noise, so the same seed, inputs and steps give the same particles.
// Positions are in the system's space, or in scene space for worldspace
// systems; setTransform() maps the system's space to the scene. Children
// are systems of their own (forEachSystem visits them for drawing). No
// GPU.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
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
    uint32_t id = 0;     // unique in its system, from 1 (children follow it)
    Vec3 waveOffset{0, 0, 0};  // oscillateposition's current offset
    float linePlace = -1;      // place on a control point line (sections 4, 5)
    std::vector<Vec3> trail;   // rope trails: past positions, newest first
};

// The layer's override factors (section 2), already filtered by the
// system's "disable override" flags. Children get the same factors.
struct ParticleFactors {
    float count = 1, rate = 1, speed = 1, size = 1, lifetime = 1, alpha = 1;
    Vec3 color{1, 1, 1};
};

// The renderer settings of a particle file (section 6): the first
// renderer's kind and its parameters with their defaults.
struct ParticleRendering {
    enum class Kind { Sprite, SpriteTrail, Rope, RopeTrail } kind = Kind::Sprite;
    std::string orientation = "screen";
    Vec3 axis{0, 0, 1};
    float length = 1, maxLength = 10, minLength = 0;  // sprite trails
    int subdivision = 0;                              // ropes
    float trailLength = 0.5f;                         // rope trails: seconds kept
    int segments = 8;                                 // rope trails: positions kept
};
ParticleRendering particleRendering(const ParticleFile& file);

// Child particle files by path (section 9); null when not loaded.
using ParticleFileLookup = std::function<const ParticleFile*(const std::string& path)>;

class ParticleSystem {
public:
    static constexpr int kMaxDepth = 4;  // child nesting levels (section 9)

    // Children are created only with a lookup.
    ParticleSystem(const ParticleFile& file, uint64_t seed, ParticleFileLookup lookup = {});
    ~ParticleSystem();
    ParticleSystem(const ParticleSystem&) = delete;
    ParticleSystem& operator=(const ParticleSystem&) = delete;

    // Inputs, read by the next step(); they reach the children too.
    void setFactors(const ParticleFactors& factors);
    // System space to scene space (the layer's world transform).
    void setTransform(const Mat4& model);
    // The cursor in scene space (control points locked to the pointer).
    void setPointer(const Vec3& scene);
    // Layer override controlpoint<index>: replaces that point's offset.
    void setControlPointOffset(int index, const Vec3& offset);

    // dt seconds of scene time; the rate factor applies inside.
    void step(double dt);
    void prewarm();

    const std::vector<Particle>& particles() const { return particles_; }
    const ParticleFile& file() const { return file_; }
    double time() const { return time_; }
    bool worldSpace() const { return file_.has(ParticleFile::kWorldSpace); }
    const Mat4& transform() const { return model_; }
    // A control point in the system's space after the last step.
    Vec3 controlPoint(int index) const;
    // Rope trails: how far the current keeping interval has run, 0..1.
    float trailFraction() const;
    // Emitters done (or stopped) and no particles or children left.
    bool finished() const;
    // Stops emitting for good (a followed particle died).
    void stopEmitting();
    // This system and every child instance, parents first.
    void forEachSystem(const std::function<void(const ParticleSystem&)>& f) const;
    size_t childInstances() const;  // live child systems, all levels

    struct Emitter;
    struct Initializer;
    struct Operator;
    struct ChildSlot;
    struct StepContext;

private:
    ParticleSystem(const ParticleFile& file, uint64_t seed, ParticleFileLookup lookup,
                   const ParticleSystem* parent, int depth);
    void placeControlPoints();
    void spawn(Emitter& emitter, std::vector<uint32_t>& born);
    void runChildren(double dt, const std::vector<uint32_t>& born, const std::vector<Vec3>& died);
    Vec3 toScene(const Vec3& sim) const;  // a position of this system's particles

    const ParticleFile& file_;
    uint64_t rng_;
    uint32_t noiseSeed_;  // seeds the noise fields; derived from the system seed
    ParticleFileLookup lookup_;
    const ParticleSystem* parent_ = nullptr;
    int depth_ = 0;
    ParticleFactors factors_;
    Mat4 model_ = Mat4::identity(), modelInverse_ = Mat4::identity();
    Vec3 pointer_{0, 0, 0};
    bool hasPointer_ = false;
    double time_ = 0;
    bool stopped_ = false;
    uint32_t nextId_ = 1;
    float trailClock_ = 0;  // seconds into the current keeping interval
    ParticleRendering rendering_;
    bool placed_ = false;  // control points placed at least once
    struct Point {
        Vec3 sys{0, 0, 0}, sim{0, 0, 0}, previous{0, 0, 0};
        bool overridden = false, fromParticle = false;
        Vec3 offset{0, 0, 0};    // layer override
        Vec3 particle{0, 0, 0};  // scene position set by the parent (set control points)
    };
    std::array<Point, 8> points_;
    std::vector<Particle> particles_;
    std::vector<std::unique_ptr<Emitter>> emitters_;
    std::vector<std::unique_ptr<Initializer>> initializers_;
    std::vector<std::unique_ptr<Operator>> operators_;
    std::vector<std::unique_ptr<ChildSlot>> children_;
};

// What a particle file uses beyond phase P5a (section 7): control points
// (components, emitters around a point other than 0, points that follow
// the pointer, are scene positions or copy the parent's), children, trail
// and rope renderers or orientations other than screen. `unknown` names
// components no phase handles (reported and skipped).
struct ParticleNeeds {
    bool controlPoints = false, children = false, trails = false;
    std::vector<std::string> unknown;
};
ParticleNeeds particleNeeds(const ParticleFile& file);


// Smooth value noise in -1..1 (section 5), deterministic for a seed.
float valueNoise(float x, float y, float z, uint32_t seed);

}  // namespace canvas::scene
