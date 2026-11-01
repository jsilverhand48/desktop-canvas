// Implementation of the particle simulation declared in ParticleSystem.h.
// Section numbers refer to src/scene/particles/README.md.
#include "scene/particles/ParticleSystem.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>

namespace canvas::scene {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

uint64_t splitmix(uint64_t& state) {
    uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// Uniform in [0, 1).
float uniform(uint64_t& state) { return float(splitmix(state) >> 40) / float(1ull << 24); }

// A fixed random in [0, 1) for (seed, a, b): per particle operator values.
float hashed(uint32_t seed, uint32_t a, uint32_t b) {
    uint64_t s = (uint64_t(seed) << 32) ^ (uint64_t(a) << 16) ^ b;
    return uniform(s);
}

float lerp(float a, float b, float t) { return a + (b - a) * t; }

// min + (max - min) * r^exponent (section 4).
float biased(float lo, float hi, float r, float exponent) {
    return lo + (hi - lo) * std::pow(r, exponent > 0 ? exponent : 1.0f);
}

Vec3 operator+(Vec3 a, Vec3 b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3 operator*(Vec3 a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
Vec3 mul(Vec3 a, Vec3 b) { return {a[0] * b[0], a[1] * b[1], a[2] * b[2]}; }
float length(Vec3 a) { return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }

// Linear ramps of section 5's operator blending; 1 when not set.
struct BlendWindow {
    float inStart = -1, inEnd = -1, outStart = -1, outEnd = -1;
    explicit BlendWindow(const ParticleComponent& c)
        : inStart(float(c.number("blendinstart", -1))), inEnd(float(c.number("blendinend", -1))),
          outStart(float(c.number("blendoutstart", -1))), outEnd(float(c.number("blendoutend", -1))) {}
    float at(float f) const {
        float w = 1;
        if (inEnd >= 0) {
            float s = std::max(0.0f, inStart);
            w *= inEnd > s ? std::clamp((f - s) / (inEnd - s), 0.0f, 1.0f) : (f >= inEnd ? 1.0f : 0.0f);
        }
        if (outStart >= 0) {
            float e = outEnd >= 0 ? outEnd : 1.0f;
            w *= e > outStart ? std::clamp((e - f) / (e - outStart), 0.0f, 1.0f) : (f < outStart ? 1.0f : 0.0f);
        }
        return w;
    }
};

}  // namespace

float valueNoise(float x, float y, float z, uint32_t seed) {
    auto lattice = [seed](int64_t i, int64_t j, int64_t k) {
        uint64_t s = uint64_t(i) * 0x9E3779B1ull ^ uint64_t(j) * 0x85EBCA77ull ^
                     uint64_t(k) * 0xC2B2AE3Dull ^ (uint64_t(seed) << 32);
        return uniform(s) * 2 - 1;
    };
    const float fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
    const int64_t ix = int64_t(fx), iy = int64_t(fy), iz = int64_t(fz);
    auto smooth = [](float t) { return t * t * (3 - 2 * t); };
    const float tx = smooth(x - fx), ty = smooth(y - fy), tz = smooth(z - fz);
    float c[2][2];
    for (int dj = 0; dj < 2; dj++)
        for (int dk = 0; dk < 2; dk++)
            c[dj][dk] = lerp(lattice(ix, iy + dj, iz + dk), lattice(ix + 1, iy + dj, iz + dk), tx);
    return lerp(lerp(c[0][0], c[1][0], ty), lerp(c[0][1], c[1][1], ty), tz);
}

namespace {

Vec3 noiseVector(Vec3 p, uint32_t seed) {
    return {valueNoise(p[0], p[1], p[2], seed), valueNoise(p[0] + 31.7f, p[1] + 17.3f, p[2], seed + 1),
            valueNoise(p[0], p[1] + 47.1f, p[2] + 11.9f, seed + 2)};
}

}  // namespace

// ------------------------------------------------------------- emitters

struct ParticleSystem::Emitter {
    bool box = false;
    float rate = 0;
    int64_t instantaneous = 0;
    double duration = 0, delay = 0;
    bool onePerFrame = false, periodic = false, audio = false;
    double minPeriod = 0, maxPeriod = 0, minPause = 0, maxPause = 0;
    int64_t maxPerPeriod = 0;
    Vec3 origin{0, 0, 0}, directions{1, 1, 1}, sign{0, 0, 0};
    float distanceMin = 0;
    Vec3 distanceMax{0, 0, 0};
    float speedMin = 0, speedMax = 0;
    // State.
    double elapsed = 0, owed = 0;
    bool burstDone = false;
    bool emitting = true;
    double phaseLeft = 0;
    int64_t emittedThisPeriod = 0;

    explicit Emitter(const ParticleComponent& c) {
        box = c.name == "boxrandom";
        rate = float(c.number("rate", 0));
        instantaneous = std::max<int64_t>(0, c.integer("instantaneous", 0));
        duration = c.number("duration", 0);
        delay = std::max(0.0, c.number("delay", 0));
        const int64_t flags = c.integer("flags", 0);
        onePerFrame = (flags & 2) != 0;
        periodic = (flags & 4) != 0;
        minPeriod = c.number("minperiodicduration", 0);
        maxPeriod = c.number("maxperiodicduration", minPeriod);
        minPause = c.number("minperiodicdelay", 0);
        maxPause = c.number("maxperiodicdelay", minPause);
        maxPerPeriod = c.integer("maxtoemitperperiod", 0);
        audio = c.integer("audioprocessingmode", 0) != 0;
        origin = c.vec3("origin", origin);
        directions = c.vec3("directions", directions);
        sign = c.vec3("sign", sign);
        distanceMin = float(c.number("distancemin", 0));
        distanceMax = c.vec3("distancemax", {0, 0, 0});
        speedMin = float(c.number("speedmin", 0));
        speedMax = float(c.number("speedmax", speedMin));
    }

    // A random emission (on) or pause length; at least 1 ms.
    double phaseLength(bool on, uint64_t& rng) const {
        const double r = uniform(rng);
        const double d = on ? minPeriod + (maxPeriod - minPeriod) * r : minPause + (maxPause - minPause) * r;
        return std::max(d, 1e-3);
    }

    // How many particles to emit this step (section 3).
    int64_t update(double dt, float countFactor, uint64_t& rng) {
        if (audio) return 0;  // active only while audio plays; phase P7
        elapsed += dt;
        const double active = elapsed - delay;
        if (active < 0) return 0;
        int64_t n = 0;
        if (!burstDone) {
            burstDone = true;
            n += instantaneous;
            if (periodic) phaseLeft = phaseLength(true, rng);
        }
        if (duration > 0 && active > duration) return n;
        if (periodic) {
            // The step counts in the phase it starts in; leftover time
            // carries into the next phase.
            const bool on = emitting;
            phaseLeft -= dt;
            while (phaseLeft <= 0) {
                emitting = !emitting;
                if (emitting) emittedThisPeriod = 0;  // a new period
                phaseLeft += phaseLength(emitting, rng);
            }
            if (!on) {
                owed = 0;
                return n;
            }
        }
        owed += double(rate) * double(countFactor) * dt;
        int64_t whole = int64_t(std::floor(owed));
        owed -= double(whole);
        if (periodic && maxPerPeriod > 0) {
            whole = std::min(whole, maxPerPeriod - emittedThisPeriod);
            emittedThisPeriod += std::max<int64_t>(0, whole);
        }
        n += std::max<int64_t>(0, whole);
        if (onePerFrame) n = std::min<int64_t>(n, 1);
        return n;
    }

    // Spawn point and initial velocity (section 3).
    void place(Particle& p, uint64_t& rng) const {
        Vec3 offset{0, 0, 0};
        if (box) {
            for (size_t k = 0; k < 3; k++)
                offset[k] = (uniform(rng) * 2 - 1) * distanceMax[k] * directions[k];
        } else {
            // A direction uniform over the sphere.
            float z = uniform(rng) * 2 - 1, phi = uniform(rng) * 2 * kPi;
            float r = std::sqrt(std::max(0.0f, 1 - z * z));
            Vec3 dir{r * std::cos(phi), r * std::sin(phi), z};
            float distance = lerp(distanceMin, distanceMax[0], uniform(rng));
            offset = mul(dir * distance, directions);
        }
        for (size_t k = 0; k < 3; k++) {
            if (sign[k] > 0) offset[k] = std::fabs(offset[k]);
            if (sign[k] < 0) offset[k] = -std::fabs(offset[k]);
        }
        p.position = origin + offset;
        if (speedMax > 0 || speedMin > 0) {
            float len = length(offset);
            Vec3 dir = len > 1e-6f ? offset * (1 / len) : Vec3{0, 1, 0};
            p.velocity = dir * lerp(speedMin, speedMax, uniform(rng));
        }
    }
};

// ---------------------------------------------------------- initializers

struct ParticleSystem::Initializer {
    enum class Kind { Lifetime, Size, Color, Alpha, Velocity, Rotation, AngularVelocity, Turbulent,
                      PositionOffset, Unsupported } kind = Kind::Unsupported;
    float minF = 0, maxF = 0, exponent = 1;
    Vec3 minV{0, 0, 0}, maxV{0, 0, 0};
    bool scalar = false;  // rotationrandom given as a number (z)
    // turbulentvelocityrandom and positionoffsetrandom
    float scale = 1, timescale = 1, offset = 0, speedMin = 0, speedMax = 0, phaseMax = 0, distance = 0;
    Vec3 forward{0, 1, 0};

    explicit Initializer(const ParticleComponent& c) {
        const std::string& n = c.name;
        exponent = float(c.number("exponent", 1));
        auto range = [&](float lo, float hi) {
            minF = float(c.number("min", lo));
            maxF = float(c.number("max", hi));
        };
        auto vrange = [&](Vec3 lo, Vec3 hi) {
            minV = c.vec3("min", lo);
            maxV = c.vec3("max", c.has("max") ? hi : minV);
        };
        if (n == "lifetimerandom") {
            kind = Kind::Lifetime;
            range(1, 1);
        } else if (n == "sizerandom") {
            kind = Kind::Size;
            range(20, 20);
        } else if (n == "alpharandom") {
            kind = Kind::Alpha;
            range(1, 1);
        } else if (n == "colorrandom") {
            kind = Kind::Color;
            vrange({255, 255, 255}, {255, 255, 255});
        } else if (n == "velocityrandom") {
            kind = Kind::Velocity;
            vrange({0, 0, 0}, {0, 0, 0});
        } else if (n == "angularvelocityrandom") {
            kind = Kind::AngularVelocity;
            vrange({0, 0, 0}, {0, 0, 0});
        } else if (n == "rotationrandom") {
            kind = Kind::Rotation;
            // A number is the z rotation; vectors give all three.
            auto value = [&](const char* key) -> Vec3 {
                const auto& v = c.params[key];
                if (v.is_number()) return {0, 0, v.get<float>()};
                return c.vec3(key, {0, 0, 0});
            };
            minV = c.has("min") ? value("min") : Vec3{0, 0, 0};
            maxV = c.has("max") ? value("max") : Vec3{0, 0, 2 * kPi};
        } else if (n == "turbulentvelocityrandom") {
            kind = Kind::Turbulent;
            scale = float(c.number("scale", 1));
            timescale = float(c.number("timescale", 1));
            offset = float(c.number("offset", 0));
            speedMin = float(c.number("speedmin", 0));
            speedMax = float(c.number("speedmax", speedMin));
            phaseMax = float(c.number("phasemax", 0));
            forward = c.vec3("forward", forward);
        } else if (n == "positionoffsetrandom") {
            kind = Kind::PositionOffset;
            distance = float(c.number("distance", 0));
            scale = float(c.number("scale", 0.01));
            timescale = float(c.number("timescale", 1));
        }
    }

    // Sets a new particle's values; the layer factors other than speed
    // apply afterwards to every particle (ParticleSystem::spawn).
    void apply(Particle& p, uint64_t& rng, const ParticleFactors& f, double time,
               uint32_t noiseSeed) const {
        switch (kind) {
            case Kind::Lifetime:
                p.lifetime = std::max(0.001f, biased(minF, maxF, uniform(rng), exponent));
                break;
            case Kind::Size:
                p.baseSize = biased(minF, maxF, uniform(rng), exponent);
                break;
            case Kind::Alpha:
                p.baseAlpha = biased(minF, maxF, uniform(rng), exponent);
                break;
            case Kind::Color: {
                const float r = uniform(rng);
                for (size_t k = 0; k < 3; k++) p.baseColor[k] = biased(minV[k], maxV[k], r, exponent) / 255.0f;
                break;
            }
            case Kind::Velocity:
                for (size_t k = 0; k < 3; k++)
                    p.velocity[k] += biased(minV[k], maxV[k], uniform(rng), exponent) * f.speed;
                break;
            case Kind::AngularVelocity:
                for (size_t k = 0; k < 3; k++)
                    p.angularVelocity[k] = biased(minV[k], maxV[k], uniform(rng), exponent);
                break;
            case Kind::Rotation:
                for (size_t k = 0; k < 3; k++)
                    p.rotation[k] = biased(minV[k], maxV[k], uniform(rng), exponent);
                break;
            case Kind::Turbulent: {
                // forward turned about z by offset plus noise spread over
                // scale * pi (1 covers every direction) [doc; inference].
                const float phase = uniform(rng) * phaseMax;
                const float n = valueNoise(float(time) * timescale + phase, 0.37f, 0.71f, noiseSeed + 0x7u);
                const float angle = offset + n * scale * kPi;
                const float c = std::cos(angle), s = std::sin(angle);
                Vec3 dir{forward[0] * c - forward[1] * s, forward[0] * s + forward[1] * c, forward[2]};
                float len = length(dir);
                if (len > 1e-6f) dir = dir * (1 / len);
                p.velocity = p.velocity + dir * (lerp(speedMin, speedMax, uniform(rng)) * f.speed);
                break;
            }
            case Kind::PositionOffset: {
                Vec3 n = noiseVector(p.position * scale + Vec3{float(time) * timescale, 0, 0},
                                     noiseSeed + 0x51u);
                p.position = p.position + n * distance;
                break;
            }
            case Kind::Unsupported: break;
        }
    }
};

// ------------------------------------------------------------ operators

struct ParticleSystem::Operator {
    enum class Kind { Movement, AngularMovement, AlphaFade, SizeChange, AlphaChange, ColorChange,
                      OscillateAlpha, OscillateSize, OscillatePosition, Turbulence, CapVelocity,
                      Unsupported } kind = Kind::Unsupported;
    BlendWindow blend;
    uint32_t index = 0;
    Vec3 gravity{0, 0, 0}, force{0, 0, 0}, mask{1, 1, 0};
    Vec3 startColor{1, 1, 1}, endColor{1, 1, 1};
    float drag = 0, fadeIn = 0.5f, fadeOut = 0.5f;
    float startValue = 1, endValue = 0, startTime = 0, endTime = 1;
    float freqMin = 0, freqMax = 0, scaleMin = 0, scaleMax = 1, phaseMin = 0, phaseMax = 0;
    float speedMin = 0, speedMax = 0, noiseScale = 0.01f, timescale = 1, maxSpeed = 0;

    Operator(const ParticleComponent& c, uint32_t i) : blend(c), index(i) {
        const std::string& n = c.name;
        if (n == "movement") {
            kind = Kind::Movement;
            gravity = c.vec3("gravity", gravity);
            drag = float(c.number("drag", 0));
        } else if (n == "angularmovement") {
            kind = Kind::AngularMovement;
            force = c.vec3("force", force);
            drag = float(c.number("drag", 0));
        } else if (n == "alphafade") {
            kind = Kind::AlphaFade;
            fadeIn = float(c.number("fadeintime", 0.5));
            fadeOut = float(c.number("fadeouttime", 0.5));
        } else if (n == "sizechange" || n == "alphachange") {
            kind = n == "sizechange" ? Kind::SizeChange : Kind::AlphaChange;
            startValue = float(c.number("startvalue", 1));
            endValue = float(c.number("endvalue", 0));
            startTime = float(c.number("starttime", 0));
            endTime = float(c.number("endtime", 1));
        } else if (n == "colorchange") {
            kind = Kind::ColorChange;
            startColor = c.vec3("startvalue", startColor);
            endColor = c.vec3("endvalue", endColor);
            startTime = float(c.number("starttime", 0));
            endTime = float(c.number("endtime", 1));
        } else if (n == "oscillatealpha" || n == "oscillatesize" || n == "oscillateposition") {
            kind = n == "oscillatealpha" ? Kind::OscillateAlpha
                   : n == "oscillatesize" ? Kind::OscillateSize
                                          : Kind::OscillatePosition;
            freqMin = float(c.number("frequencymin", 0));
            freqMax = float(c.number("frequencymax", freqMin));
            scaleMin = float(c.number("scalemin", 0));
            scaleMax = float(c.number("scalemax", 1));
            phaseMin = float(c.number("phasemin", 0));
            phaseMax = float(c.number("phasemax", phaseMin));
            mask = c.vec3("mask", mask);
        } else if (n == "turbulence") {
            kind = Kind::Turbulence;
            speedMin = float(c.number("speedmin", 0));
            speedMax = float(c.number("speedmax", speedMin));
            noiseScale = float(c.number("scale", 0.01));
            timescale = float(c.number("timescale", 1));
            mask = c.vec3("mask", {1, 1, 1});
        } else if (n == "capvelocity") {
            kind = Kind::CapVelocity;
            maxSpeed = float(c.number("maxspeed", 0));
        }
    }

    // A per particle value in [lo, hi] for slot k.
    float particleValue(const Particle& p, uint32_t k, float lo, float hi) const {
        return lerp(lo, hi, hashed(p.seed, index, k));
    }

    float wave(const Particle& p, float f) const {
        const float freq = particleValue(p, 0, freqMin, freqMax);
        const float phase = particleValue(p, 1, phaseMin, phaseMax);
        return std::sin(phase + 2 * kPi * freq * f);
    }

    float ramp(float f) const {
        if (endTime <= startTime) return f >= endTime ? 1.0f : 0.0f;
        return std::clamp((f - startTime) / (endTime - startTime), 0.0f, 1.0f);
    }
};

// --------------------------------------------------------------- system

ParticleSystem::ParticleSystem(const ParticleFile& file, uint64_t seed)
    : file_(file), rng_(seed), noiseSeed_(uint32_t(seed ^ (seed >> 32))) {
    for (const auto& e : file.emitters)
        if (e.name == "sphererandom" || e.name == "boxrandom")
            emitters_.push_back(std::make_unique<Emitter>(e));
    for (const auto& i : file.initializers) {
        auto init = std::make_unique<Initializer>(i);
        if (init->kind != Initializer::Kind::Unsupported) initializers_.push_back(std::move(init));
    }
    uint32_t index = 0;
    for (const auto& o : file.operators) {
        auto op = std::make_unique<Operator>(o, index++);
        if (op->kind != Operator::Kind::Unsupported) operators_.push_back(std::move(op));
    }
    particles_.reserve(size_t(std::min<int64_t>(file.maxCount, 4096)));
}

ParticleSystem::~ParticleSystem() = default;

ParticleNeeds particleNeeds(const ParticleFile& file) {
    // Sections 3 to 6: what P5a runs, and what P5b adds.
    static const std::set<std::string> emitters = {"sphererandom", "boxrandom"};
    static const std::set<std::string> initializers = {
        "lifetimerandom", "sizerandom",   "alpharandom",         "colorrandom",
        "velocityrandom", "rotationrandom", "angularvelocityrandom",
        "turbulentvelocityrandom", "positionoffsetrandom"};
    static const std::set<std::string> operators = {
        "movement",       "angularmovement", "alphafade",         "sizechange",
        "alphachange",    "colorchange",     "oscillatealpha",    "oscillatesize",
        "oscillateposition", "turbulence",   "capvelocity"};
    static const std::set<std::string> controlPointKinds = {
        "mapsequencebetweencontrolpoints", "mapsequencearoundcontrolpoint",
        "remapinitialvalue",               "inheritcontrolpointvelocity",
        "controlpointattract",             "vortex",
        "vortex_v2",                       "remapvalue",
        "reducemovementnearcontrolpoint",  "maintaindistancetocontrolpoint",
        "maintaindistancebetweencontrolpoints"};
    static const std::set<std::string> trailRenderers = {"spritetrail", "rope", "ropetrail"};
    ParticleNeeds needs;
    needs.children = !file.children.empty();
    auto check = [&](const std::vector<ParticleComponent>& list, const std::set<std::string>& known) {
        for (const auto& c : list) {
            if (controlPointKinds.count(c.name)) needs.controlPoints = true;
            else if (!known.count(c.name)) needs.unknown.push_back(c.name);
        }
    };
    check(file.emitters, emitters);
    check(file.initializers, initializers);
    check(file.operators, operators);
    for (const auto& e : file.emitters)
        if (e.integer("controlpoint", 0) != 0) needs.controlPoints = true;
    for (const auto& r : file.renderers) {
        const std::string orientation = r.text("orientation");
        if (trailRenderers.count(r.name) || (!orientation.empty() && orientation != "screen"))
            needs.trails = true;
        else if (r.name != "sprite")
            needs.unknown.push_back(r.name);
    }
    return needs;
}

void ParticleSystem::spawn(Emitter& emitter) {
    Particle p;
    p.seed = uint32_t(splitmix(rng_));
    emitter.place(p, rng_);
    p.velocity = p.velocity * factors_.speed;
    for (const auto& init : initializers_) init->apply(p, rng_, factors_, time_, noiseSeed_);
    // The layer factors (section 2), also for values no initializer set.
    p.lifetime *= factors_.lifetime;
    p.baseSize *= factors_.size;
    p.baseAlpha *= factors_.alpha;
    p.baseColor = mul(p.baseColor, factors_.color);
    p.size = p.baseSize;
    p.alpha = p.baseAlpha;
    p.color = p.baseColor;
    p.frame = file_.randomFrame ? uniform(rng_) : 0;
    if (worldSpace()) {
        Vec4 at = spawn_ * Vec4{p.position[0], p.position[1], p.position[2], 1};
        Vec4 v = spawn_ * Vec4{p.velocity[0], p.velocity[1], p.velocity[2], 0};
        p.position = {at[0], at[1], at[2]};
        p.velocity = {v[0], v[1], v[2]};
    }
    particles_.push_back(p);
}

void ParticleSystem::step(double dtIn) {
    const float dt = float(std::max(0.0, dtIn) * std::max(0.0f, factors_.rate));
    if (dt <= 0) return;
    time_ += dt;
    // Emit (section 3), up to the maximum count.
    for (auto& emitter : emitters_) {
        int64_t n = emitter->update(dt, factors_.count, rng_);
        n = std::min<int64_t>(n, file_.maxCount - int64_t(particles_.size()));
        for (int64_t i = 0; i < n; i++) spawn(*emitter);
    }
    // Operate (section 5).
    for (auto& p : particles_) {
        const float f = std::clamp(p.age / p.lifetime, 0.0f, 1.0f);
        float sizeMul = 1, alphaMul = 1;
        Vec3 colorMul{1, 1, 1}, wave{0, 0, 0};
        bool moves = false;
        for (const auto& op : operators_) {
            const float w = op->blend.at(f);
            switch (op->kind) {
                case Operator::Kind::Movement:
                    p.velocity = p.velocity + op->gravity * (factors_.speed * dt * w);
                    p.velocity = p.velocity * std::exp(-op->drag * dt * w);
                    moves = true;
                    break;
                case Operator::Kind::AngularMovement:
                    p.angularVelocity = p.angularVelocity + op->force * (dt * w);
                    p.angularVelocity = p.angularVelocity * std::exp(-op->drag * dt * w);
                    p.rotation = p.rotation + p.angularVelocity * dt;
                    break;
                case Operator::Kind::AlphaFade: {
                    float a = 1;
                    if (op->fadeIn > 0 && f < op->fadeIn) a = f / op->fadeIn;
                    if (op->fadeOut < 1 && f > op->fadeOut) a *= (1 - f) / (1 - op->fadeOut);
                    alphaMul *= lerp(1, a, w);
                    break;
                }
                case Operator::Kind::SizeChange:
                    sizeMul *= lerp(1, lerp(op->startValue, op->endValue, op->ramp(f)), w);
                    break;
                case Operator::Kind::AlphaChange:
                    alphaMul *= lerp(1, lerp(op->startValue, op->endValue, op->ramp(f)), w);
                    break;
                case Operator::Kind::ColorChange: {
                    const float t = op->ramp(f);
                    for (size_t k = 0; k < 3; k++)
                        colorMul[k] *= lerp(1, lerp(op->startColor[k], op->endColor[k], t), w);
                    break;
                }
                case Operator::Kind::OscillateAlpha:
                    alphaMul *= lerp(1, lerp(op->scaleMin, op->scaleMax, 0.5f + 0.5f * op->wave(p, f)), w);
                    break;
                case Operator::Kind::OscillateSize:
                    sizeMul *= lerp(1, lerp(op->scaleMin, op->scaleMax, 0.5f + 0.5f * op->wave(p, f)), w);
                    break;
                case Operator::Kind::OscillatePosition: {
                    const float scale = op->particleValue(p, 2, op->scaleMin, op->scaleMax);
                    wave = wave + op->mask * (op->wave(p, f) * scale * w);
                    break;
                }
                case Operator::Kind::Turbulence: {
                    const float speed = op->particleValue(p, 3, op->speedMin, op->speedMax);
                    Vec3 n = noiseVector(p.position * op->noiseScale +
                                             Vec3{float(time_) * op->timescale, 0, 0},
                                         noiseSeed_ + 0x1000u + op->index);
                    p.velocity = p.velocity + mul(n, op->mask) * (speed * factors_.speed * dt * w);
                    break;
                }
                case Operator::Kind::CapVelocity: {
                    const float len = length(p.velocity);
                    if (op->maxSpeed > 0 && len > op->maxSpeed)
                        p.velocity = p.velocity * (op->maxSpeed / len);
                    break;
                }
                case Operator::Kind::Unsupported: break;
            }
        }
        // Velocity moves particles only with a movement operator [doc].
        if (moves) p.position = p.position + p.velocity * dt;
        p.position = p.position + (wave - p.waveOffset);
        p.waveOffset = wave;
        p.size = p.baseSize * sizeMul;
        p.alpha = p.baseAlpha * alphaMul;
        p.color = mul(p.baseColor, colorMul);
        if (!file_.randomFrame) p.frame = f * float(file_.sequenceMultiplier);
        p.age += dt;
    }
    // Age out.
    std::erase_if(particles_, [](const Particle& p) { return p.age >= p.lifetime; });
}

void ParticleSystem::prewarm() {
    const double step = 1.0 / 30;
    for (double t = 0; t < file_.startTime; t += step) {
        // The rate factor applies inside step(); prewarm runs scene time.
        this->step(step);
    }
}

}  // namespace canvas::scene
