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
float dot(Vec3 a, Vec3 b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3 cross(Vec3 a, Vec3 b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Vec3 normalized(Vec3 v, Vec3 fallback) {
    const float l = length(v);
    return l > 1e-6f ? v * (1 / l) : fallback;
}
Vec3 transformPoint(const Mat4& m, Vec3 p) {
    const Vec4 r = m * Vec4{p[0], p[1], p[2], 1};
    return {r[0], r[1], r[2]};
}
Vec3 transformVector(const Mat4& m, Vec3 v) {
    const Vec4 r = m * Vec4{v[0], v[1], v[2], 0};
    return {r[0], r[1], r[2]};
}
// v turned about the unit axis by angle radians, right handed (Rodrigues).
Vec3 rotateAbout(Vec3 v, Vec3 axis, float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    return v * c + cross(axis, v) * s + axis * (dot(axis, v) * (1 - c));
}
// A unit vector across the unit axis: x for the z axis (section 4).
Vec3 firstAxis(Vec3 axis) {
    const Vec3 x{1, 0, 0}, y{0, 1, 0};
    return std::fabs(axis[0]) < 0.9f ? normalized(x - axis * axis[0], y) : normalized(y - axis * axis[1], x);
}
// The sequence value u of the n-th placement (section 4).
float sequenceValue(uint64_t n, float count, bool mirror) {
    const double s = double(n) / std::max(1e-3, double(count));
    if (!mirror) return float(s - std::floor(s));
    const double half = s / 2 - std::floor(s / 2);
    return float(1 - std::fabs(1 - 2 * half));
}

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

// remapinitialvalue and remapvalue (sections 4 and 5).
struct Remap {
    enum class Input { Life, SystemTime, Distance, Other } input = Input::Life;
    enum class Output { Size, Opacity, Velocity, Speed, Lifetime, Other } output = Output::Size;
    enum class Operation { Assign, Multiply, Add, Subtract } operation = Operation::Assign;
    enum class Transform { None, Sine, Simplex, Fbm } transform = Transform::None;
    int inputPoint = 0;
    float inLo = 0, inHi = 1, inputScale = 1;
    Vec3 outLo{0, 0, 0}, outHi{1, 1, 1};
    int64_t flags = 0;  // 1 clamps the input, 2 the output

    Remap(const ParticleComponent& c, Output fallback) {
        const std::string in = c.text("input");
        input = in == "particlesystemtime"       ? Input::SystemTime
                : in == "distancetocontrolpoint" ? Input::Distance
                : in.empty() || in == "particlelifetime" ? Input::Life
                                                         : Input::Other;
        const std::string out = c.text("output");
        output = out.empty()                            ? fallback
                 : out == "size"                        ? Output::Size
                 : out == "opacity" || out == "alpha"   ? Output::Opacity
                 : out == "velocity"                    ? Output::Velocity
                 : out == "speed"                       ? Output::Speed
                 : out == "lifetime"                    ? Output::Lifetime
                                                        : Output::Other;
        const std::string op = c.text("operation");
        operation = op == "multiply"   ? Operation::Multiply
                    : op == "add"      ? Operation::Add
                    : op == "subtract" ? Operation::Subtract
                                       : Operation::Assign;  // remap, assign, absent
        const std::string tf = c.text("transformfunction");
        transform = tf == "sine"           ? Transform::Sine
                    : tf == "simplexnoise" ? Transform::Simplex
                    : tf == "fbmnoise"     ? Transform::Fbm
                                           : Transform::None;
        inputPoint = int(std::clamp<int64_t>(c.integer("inputcontrolpoint0", 0), 0, 7));
        inLo = float(c.number("inputrangemin", 0));
        inHi = float(c.number("inputrangemax", 1));
        inputScale = float(c.number("transforminputscale", 1));
        outLo = c.vec3("outputrangemin", outLo);
        outHi = c.vec3("outputrangemax", outHi);
        flags = c.integer("flags", 0);
    }

    // The input mapped to 0..1 (beyond when not clamped), through the
    // transform function with a phase per particle.
    float map(float value, float phase, uint32_t noiseSeed) const {
        float x = value;
        if (inHi != inLo) x = (x - inLo) / (inHi - inLo);
        if (flags & 1) x = std::clamp(x, 0.0f, 1.0f);
        if (transform == Transform::None) return x;
        const float v = x * inputScale + phase;
        float f = 0;
        switch (transform) {
            case Transform::Sine: f = std::sin(v); break;
            case Transform::Simplex: f = valueNoise(v, 0.5f, 0.5f, noiseSeed); break;
            case Transform::Fbm:
                f = (valueNoise(v, 0.5f, 0.5f, noiseSeed) + 0.5f * valueNoise(2 * v, 0.5f, 0.5f, noiseSeed) +
                     0.25f * valueNoise(4 * v, 0.5f, 0.5f, noiseSeed)) / 1.75f;
                break;
            case Transform::None: break;
        }
        return 0.5f + 0.5f * f;
    }

    // The output range at x, clamped to it with flag 2.
    Vec3 value(float x) const {
        Vec3 y{};
        for (size_t k = 0; k < 3; k++) {
            y[k] = outLo[k] + (outHi[k] - outLo[k]) * x;
            if (flags & 2) y[k] = std::clamp(y[k], std::min(outLo[k], outHi[k]), std::max(outLo[k], outHi[k]));
        }
        return y;
    }

    float combine(float current, float y) const {
        switch (operation) {
            case Operation::Assign: return y;
            case Operation::Multiply: return current * y;
            case Operation::Add: return current + y;
            case Operation::Subtract: return current - y;
        }
        return y;
    }
};

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
    int controlPoint = 0;  // the spawn centre (section 3)
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
        controlPoint = int(std::clamp<int64_t>(c.integer("controlpoint", 0), 0, 7));
    }

    // Done when it can emit no more: audio emitters (until phase P7), a
    // burst without a rate, or a duration that is over.
    bool finished() const {
        if (audio) return true;
        if (burstDone && rate <= 0) return true;
        return duration > 0 && elapsed - delay > duration;
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

    // Spawn point around centre (the control point) and initial velocity
    // (section 3), in the system's space.
    void place(Particle& p, uint64_t& rng, Vec3 centre) const {
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
        p.position = centre + origin + offset;
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
                      PositionOffset, MapBetween, MapAround, RemapInitial, Unsupported } kind =
        Kind::Unsupported;
    float minF = 0, maxF = 0, exponent = 1;
    Vec3 minV{0, 0, 0}, maxV{0, 0, 0};
    bool scalar = false;  // rotationrandom given as a number (z)
    // turbulentvelocityrandom and positionoffsetrandom
    float scale = 1, timescale = 1, offset = 0, speedMin = 0, speedMax = 0, phaseMax = 0, distance = 0;
    Vec3 forward{0, 1, 0};
    // mapsequencebetweencontrolpoints and mapsequencearoundcontrolpoint
    int point = 0, pointEnd = 1;
    float count = 10, boundsLo = 0, boundsHi = 1, arcAmount = 0.1f;
    bool mirror = false;
    int64_t flags = 0;
    Vec3 axis{0, 0, 1}, speedMinV{0, 0, 0}, speedMaxV{0, 0, 0};
    uint64_t placed = 0;  // particles placed so far (the sequence)
    std::unique_ptr<Remap> remap;  // remapinitialvalue

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
        } else if (n == "mapsequencebetweencontrolpoints" || n == "mapsequencearoundcontrolpoint") {
            const bool between = n == "mapsequencebetweencontrolpoints";
            kind = between ? Kind::MapBetween : Kind::MapAround;
            count = float(c.number("count", 10));
            mirror = c.text("limitbehavior") == "mirror";
            const Vec3 bounds = c.vec3("bounds", {0, 1, 0});
            boundsLo = bounds[0];
            boundsHi = bounds[1];
            flags = c.integer("flags", 0);
            arcAmount = float(c.number("arcamount", 0.1));
            point = int(std::clamp<int64_t>(c.integer(between ? "controlpointstart" : "controlpoint", 0), 0, 7));
            pointEnd = int(std::clamp<int64_t>(c.integer("controlpointend", 1), 0, 7));
            axis = normalized(c.vec3("axis", axis), {0, 0, 1});
            speedMinV = c.vec3("speedmin", speedMinV);
            speedMaxV = c.vec3("speedmax", c.has("speedmax") ? speedMaxV : speedMinV);
        } else if (n == "remapinitialvalue") {
            kind = Kind::RemapInitial;
            remap = std::make_unique<Remap>(c, Remap::Output::Size);
        }
    }

    // Sets a new particle's values in the system's space; centre is the
    // emitter's centre. The layer factors other than speed apply
    // afterwards to every particle (ParticleSystem::spawn).
    void apply(Particle& p, uint64_t& rng, const ParticleSystem& sys, Vec3 centre);
};

void ParticleSystem::Initializer::apply(Particle& p, uint64_t& rng, const ParticleSystem& sys,
                                        Vec3 centre) {
    const ParticleFactors& f = sys.factors_;
    const double time = sys.time_;
    const uint32_t noiseSeed = sys.noiseSeed_;
    {
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
            case Kind::MapBetween: {
                // Section 4: the next place on the line, the emitter offset
                // kept (and reduced near the ends with flag 1).
                const float u = sequenceValue(placed++, count, mirror);
                const float at = boundsLo + (boundsHi - boundsLo) * u;
                const Vec3 a = sys.points_[size_t(point)].sys, b = sys.points_[size_t(pointEnd)].sys;
                const float edge = std::sin(kPi * std::clamp(at, 0.0f, 1.0f));
                Vec3 offset = p.position - centre;
                if (flags & 1) offset = offset * edge;
                Vec3 linePoint = a + (b - a) * at;
                if (flags & 8) {
                    const Vec3 d = b - a;
                    const Vec3 left = normalized(Vec3{-d[1], d[0], 0}, {0, 0, 0});
                    linePoint = linePoint + left * (arcAmount * length(d) * 4 * at * (1 - at));
                }
                p.position = linePoint + offset;
                if (flags & 2) p.velocity = p.velocity * edge;
                if (flags & 4) p.baseSize *= edge;
                p.linePlace = at;
                break;
            }
            case Kind::MapAround: {
                // Section 4: the next angle on a circle about the axis; the
                // radius is the emitter offset across the axis.
                const float u = sequenceValue(placed++, count, mirror);
                const float angle = 2 * kPi * (boundsLo + (boundsHi - boundsLo) * u);
                const Vec3 c = sys.points_[size_t(point)].sys;
                const Vec3 e1 = firstAxis(axis), e2 = cross(axis, e1);
                const Vec3 offset = p.position - centre;
                const float along = dot(offset, axis);
                const float radius = length(offset - axis * along);
                const Vec3 radial = e1 * std::cos(angle) + e2 * std::sin(angle);
                const Vec3 tangent = e2 * std::cos(angle) - e1 * std::sin(angle);
                p.position = c + radial * radius + axis * along;
                Vec3 speed;
                for (size_t k = 0; k < 3; k++) speed[k] = lerp(speedMinV[k], speedMaxV[k], uniform(rng));
                p.velocity = p.velocity + (tangent * speed[0] + radial * speed[1] + axis * speed[2]) * f.speed;
                break;
            }
            case Kind::RemapInitial: {
                float in = 0;
                if (remap->input == Remap::Input::Distance)
                    in = length(p.position - sys.points_[size_t(remap->inputPoint)].sys);
                const float y = remap->value(remap->map(in, 0, noiseSeed))[0];
                switch (remap->output) {
                    case Remap::Output::Size: p.baseSize = remap->combine(p.baseSize, y); break;
                    case Remap::Output::Opacity: p.baseAlpha = remap->combine(p.baseAlpha, y); break;
                    case Remap::Output::Lifetime:
                        p.lifetime = std::max(0.001f, remap->combine(p.lifetime, y));
                        break;
                    default: break;  // velocity, speed and others: not at creation
                }
                break;
            }
            case Kind::Unsupported: break;
        }
    }
}

// ------------------------------------------------------------ operators

struct ParticleSystem::Operator {
    enum class Kind { Movement, AngularMovement, AlphaFade, SizeChange, AlphaChange, ColorChange,
                      OscillateAlpha, OscillateSize, OscillatePosition, Turbulence, CapVelocity,
                      Attract, Vortex, Remap, ReduceMovement, MaintainDistance, MaintainBetween,
                      Unsupported } kind = Kind::Unsupported;
    BlendWindow blend;
    uint32_t index = 0;
    Vec3 gravity{0, 0, 0}, force{0, 0, 0}, mask{1, 1, 0};
    Vec3 startColor{1, 1, 1}, endColor{1, 1, 1};
    float drag = 0, fadeIn = 0.5f, fadeOut = 0.5f;
    float startValue = 1, endValue = 0, startTime = 0, endTime = 1;
    float freqMin = 0, freqMax = 0, scaleMin = 0, scaleMax = 1, phaseMin = 0, phaseMax = 0;
    float speedMin = 0, speedMax = 0, noiseScale = 0.01f, timescale = 1, maxSpeed = 0;
    // Control point operators (section 5).
    int point = 0, pointEnd = 1;
    Vec3 origin{0, 0, 0}, axis{0, 0, 1};
    float threshold = 512, strength = 0;  // controlpointattract: distance, scale
    float distanceInner = 0, distanceOuter = 0, inner = 0, outer = 0;  // speeds or reductions
    bool cylinder = false, ring = false, audio = false;
    float ringRadius = 256, ringWidth = 0, ringPull = 0, ringForce = 1000;
    float distance = 256;  // maintaindistancetocontrolpoint; strength is variablestrength
    std::unique_ptr<Remap> remap;

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
        } else if (n == "controlpointattract") {
            kind = Kind::Attract;
            point = int(std::clamp<int64_t>(c.integer("controlpoint", 0), 0, 7));
            origin = c.vec3("origin", origin);
            threshold = float(c.number("threshold", 512));
            strength = float(c.number("scale", 0));
        } else if (n == "vortex" || n == "vortex_v2") {
            kind = Kind::Vortex;
            point = int(std::clamp<int64_t>(c.integer("controlpoint", 0), 0, 7));
            axis = normalized(c.vec3("axis", axis), {0, 0, 1});
            const int64_t flags = c.integer("flags", 0);
            cylinder = (flags & 1) != 0;
            ring = n == "vortex_v2" && (flags & 2) != 0;
            distanceInner = float(c.number("distanceinner", 0));
            distanceOuter = float(c.number("distanceouter", 0));
            inner = float(c.number("speedinner", 0));
            outer = float(c.number("speedouter", 0));
            ringRadius = float(c.number("ringradius", 256));
            ringWidth = float(c.number("ringwidth", 0));
            ringPull = float(c.number("ringpulldistance", 0));
            ringForce = float(c.number("ringpullforce", 1000));
            audio = c.integer("audioprocessingmode", 0) != 0;
        } else if (n == "remapvalue") {
            kind = Kind::Remap;
            remap = std::make_unique<Remap>(c, Remap::Output::Opacity);
            point = remap->inputPoint;
        } else if (n == "reducemovementnearcontrolpoint") {
            kind = Kind::ReduceMovement;
            point = int(std::clamp<int64_t>(c.integer("controlpoint", 0), 0, 7));
            distanceInner = float(c.number("distanceinner", 0));
            distanceOuter = float(c.number("distanceouter", 0));
            inner = float(c.number("reductioninner", 0));
            outer = float(c.number("reductionouter", 0));
        } else if (n == "maintaindistancetocontrolpoint") {
            kind = Kind::MaintainDistance;
            point = int(std::clamp<int64_t>(c.integer("controlpoint", 0), 0, 7));
            distance = float(c.number("distance", 256));
            strength = float(c.number("variablestrength", 1));
        } else if (n == "maintaindistancebetweencontrolpoints") {
            kind = Kind::MaintainBetween;
            point = int(std::clamp<int64_t>(c.integer("controlpointstart", 0), 0, 7));
            pointEnd = int(std::clamp<int64_t>(c.integer("controlpointend", 1), 0, 7));
        }
    }

    // The value between inner and outer by distance d (vortex speeds,
    // movement reductions): inner up to distanceInner, outer from
    // distanceOuter on, linear between.
    float byDistance(float d) const {
        if (d <= distanceInner) return inner;
        if (d >= distanceOuter || distanceOuter <= distanceInner) return outer;
        return lerp(inner, outer, (d - distanceInner) / (distanceOuter - distanceInner));
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

// ------------------------------------------------------------- children

struct ParticleSystem::ChildSlot {
    enum class Type { Static, Spawn, Follow, Death } type = Type::Static;
    const ParticleChild& def;
    const ParticleFile& file;
    Mat4 local;  // origin, angles (z) and scale (section 9)
    size_t maxCount = 10;
    struct Instance {
        std::unique_ptr<ParticleSystem> system;
        uint32_t follow = 0;   // the followed particle's id; 0 none
        Vec3 place{0, 0, 0};   // in the parent's simulation space
        bool warmed = false;   // its own start time has run
    };
    std::vector<Instance> instances;

    ChildSlot(const ParticleChild& d, const ParticleFile& f) : def(d), file(f) {
        type = d.type == "eventspawn"    ? Type::Spawn
               : d.type == "eventfollow" ? Type::Follow
               : d.type == "eventdeath"  ? Type::Death
                                         : Type::Static;
        local = Mat4::translate(d.origin[0], d.origin[1], d.origin[2]) * Mat4::rotateZ(d.angles[2]) *
                Mat4::scale(d.scale[0], d.scale[1], d.scale[2]);
        maxCount = d.maxCount > 0 ? size_t(d.maxCount) : 10;
    }
};

// --------------------------------------------------------------- system

ParticleSystem::ParticleSystem(const ParticleFile& file, uint64_t seed, ParticleFileLookup lookup)
    : ParticleSystem(file, seed, std::move(lookup), nullptr, 0) {}

ParticleSystem::ParticleSystem(const ParticleFile& file, uint64_t seed, ParticleFileLookup lookup,
                               const ParticleSystem* parent, int depth)
    : file_(file),
      rng_(seed),
      noiseSeed_(uint32_t(seed ^ (seed >> 32))),
      lookup_(std::move(lookup)),
      parent_(parent),
      depth_(depth),
      rendering_(particleRendering(file)) {
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
    // Children (section 9): static ones live with this system.
    if (lookup_ && depth_ < kMaxDepth) {
        for (const auto& child : file.children) {
            const ParticleFile* f = lookup_(child.file);
            if (!f || f == &file_) continue;  // missing, or naming itself
            auto slot = std::make_unique<ChildSlot>(child, *f);
            if (slot->type == ChildSlot::Type::Static) {
                ChildSlot::Instance instance;
                instance.system.reset(new ParticleSystem(*f, splitmix(rng_), lookup_, this, depth_ + 1));
                slot->instances.push_back(std::move(instance));
            }
            children_.push_back(std::move(slot));
        }
    }
}

ParticleSystem::~ParticleSystem() = default;

void ParticleSystem::setFactors(const ParticleFactors& factors) { factors_ = factors; }

void ParticleSystem::setTransform(const Mat4& model) {
    model_ = model;
    modelInverse_ = model.inverse();
}

void ParticleSystem::setPointer(const Vec3& scene) {
    pointer_ = scene;
    hasPointer_ = true;
}

void ParticleSystem::setControlPointOffset(int index, const Vec3& offset) {
    if (index < 0 || index >= int(points_.size())) return;
    points_[size_t(index)].overridden = true;
    points_[size_t(index)].offset = offset;
}

Vec3 ParticleSystem::controlPoint(int index) const {
    return index >= 0 && index < int(points_.size()) ? points_[size_t(index)].sys : Vec3{0, 0, 0};
}

float ParticleSystem::trailFraction() const {
    const float interval = rendering_.trailLength / float(std::max(1, rendering_.segments));
    return interval > 0 ? std::clamp(trailClock_ / interval, 0.0f, 1.0f) : 0.0f;
}

bool ParticleSystem::finished() const {
    if (!stopped_)
        for (const auto& e : emitters_)
            if (!e->finished()) return false;
    if (!particles_.empty()) return false;
    for (const auto& slot : children_)
        for (const auto& instance : slot->instances)
            if (!instance.system->finished()) return false;
    return true;
}

void ParticleSystem::stopEmitting() {
    stopped_ = true;
    for (auto& slot : children_)
        for (auto& instance : slot->instances) instance.system->stopEmitting();
}

void ParticleSystem::forEachSystem(const std::function<void(const ParticleSystem&)>& f) const {
    f(*this);
    for (const auto& slot : children_)
        for (const auto& instance : slot->instances) instance.system->forEachSystem(f);
}

size_t ParticleSystem::childInstances() const {
    size_t n = 0;
    for (const auto& slot : children_)
        for (const auto& instance : slot->instances) n += 1 + instance.system->childInstances();
    return n;
}

Vec3 ParticleSystem::toScene(const Vec3& sim) const { return worldSpace() ? sim : transformPoint(model_, sim); }

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
    for (const auto& cp : file.controlPoints)
        if (cp.lockToPointer || (cp.flags & (2 | 4)) != 0) needs.controlPoints = true;
    for (const auto& r : file.renderers) {
        const std::string orientation = r.text("orientation");
        if (trailRenderers.count(r.name) || (!orientation.empty() && orientation != "screen"))
            needs.trails = true;
        else if (r.name != "sprite")
            needs.unknown.push_back(r.name);
    }
    return needs;
}

ParticleRendering particleRendering(const ParticleFile& file) {
    using Kind = ParticleRendering::Kind;
    ParticleRendering r;
    if (file.renderers.empty()) return r;
    const ParticleComponent& c = file.renderers.front();
    r.kind = c.name == "spritetrail" ? Kind::SpriteTrail
             : c.name == "rope"      ? Kind::Rope
             : c.name == "ropetrail" ? Kind::RopeTrail
                                     : Kind::Sprite;
    if (const std::string o = c.text("orientation"); !o.empty()) r.orientation = o;
    r.axis = c.vec3("axis", r.axis);
    if (r.kind == Kind::RopeTrail)
        r.trailLength = std::max(0.0f, float(c.number("length", 0.5)));
    else
        r.length = float(c.number("length", 1));
    r.maxLength = float(c.number("maxlength", 10));
    r.minLength = float(c.number("minlength", 0));
    r.subdivision = int(std::clamp<int64_t>(c.integer("subdivision", 0), 0, 16));
    r.segments = int(std::clamp<int64_t>(c.integer("segments", 8), 1, 32));
    return r;
}

void ParticleSystem::placeControlPoints() {
    // Section 8, in the system's space; sim is the simulation's space.
    for (size_t i = 0; i < points_.size(); i++) {
        Point& point = points_[i];
        const ParticleControlPoint* def = nullptr;
        for (const auto& cp : file_.controlPoints)
            if (cp.id == int(i)) def = &cp;
        const int64_t flags = def ? def->flags : 0;
        Vec3 sys{0, 0, 0};
        if (point.fromParticle) {
            sys = transformPoint(modelInverse_, point.particle);
        } else if (def && def->lockToPointer) {
            if (hasPointer_) sys = transformPoint(modelInverse_, pointer_);
        } else if ((flags & 4) && parent_) {
            const size_t j = size_t(std::clamp(def->parent < 0 ? 0 : def->parent, 0, 7));
            const Vec3 parentSys = parent_->points_[j].sys;
            sys = (flags & 8) ? parentSys : transformPoint(modelInverse_, transformPoint(parent_->model_, parentSys));
        } else if (i > 0) {
            const Vec3 offset = point.overridden ? point.offset : def ? def->offset : Vec3{0, 0, 0};
            sys = (flags & 2) ? transformPoint(modelInverse_, offset) : offset;
        }
        point.sys = sys;
        const Vec3 sim = worldSpace() ? transformPoint(model_, sys) : sys;
        point.previous = placed_ ? point.sim : sim;
        point.sim = sim;
    }
    placed_ = true;
}

void ParticleSystem::spawn(Emitter& emitter, std::vector<uint32_t>& born) {
    Particle p;
    p.seed = uint32_t(splitmix(rng_));
    p.id = nextId_++;
    const Vec3 centre = points_[size_t(emitter.controlPoint)].sys;
    emitter.place(p, rng_, centre);
    p.velocity = p.velocity * factors_.speed;
    for (const auto& init : initializers_) init->apply(p, rng_, *this, centre + emitter.origin);
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
        p.position = transformPoint(model_, p.position);
        p.velocity = transformVector(model_, p.velocity);
    }
    if (rendering_.kind == ParticleRendering::Kind::RopeTrail) p.trail.push_back(p.position);
    born.push_back(p.id);
    particles_.push_back(std::move(p));
}

void ParticleSystem::step(double dtIn) {
    const float dt = float(std::max(0.0, dtIn) * std::max(0.0f, factors_.rate));
    if (dt <= 0) return;
    time_ += dt;
    placeControlPoints();
    // Emit (section 3), up to the maximum count.
    std::vector<uint32_t> born;
    if (!stopped_)
        for (auto& emitter : emitters_) {
            int64_t n = emitter->update(dt, factors_.count, rng_);
            n = std::min<int64_t>(n, file_.maxCount - int64_t(particles_.size()));
            for (int64_t i = 0; i < n; i++) spawn(*emitter, born);
        }
    // Vortex axes in the simulation's space.
    std::vector<Vec3> axes;
    axes.reserve(operators_.size());
    for (const auto& op : operators_)
        axes.push_back(worldSpace() ? normalized(transformVector(model_, op->axis), op->axis) : op->axis);
    // Operate (section 5).
    for (auto& p : particles_) {
        const float f = std::clamp(p.age / p.lifetime, 0.0f, 1.0f);
        float sizeMul = 1, alphaMul = 1;
        Vec3 colorMul{1, 1, 1}, wave{0, 0, 0};
        bool moves = false;
        for (size_t k = 0; k < operators_.size(); k++) {
            const Operator* op = operators_[k].get();
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
                    for (size_t c = 0; c < 3; c++)
                        colorMul[c] *= lerp(1, lerp(op->startColor[c], op->endColor[c], t), w);
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
                case Operator::Kind::Attract: {
                    // Section 5: a steady pull (or push) within the distance.
                    const Vec3 to = points_[size_t(op->point)].sim + op->origin - p.position;
                    const float d = length(to);
                    if (d > 1e-6f && d < op->threshold)
                        p.velocity = p.velocity + to * (op->strength * factors_.speed * dt * w / d);
                    break;
                }
                case Operator::Kind::Vortex: {
                    if (op->audio) break;  // still without audio (phase P7)
                    const Vec3 axis = axes[k];
                    const Vec3 c = points_[size_t(op->point)].sim;
                    const Vec3 r = p.position - c;
                    const float along = dot(r, axis);
                    const Vec3 across = r - axis * along;
                    const float radial = length(across);
                    float d = op->cylinder ? radial : length(r);
                    if (op->ring) {
                        const float fromRing = std::fabs(radial - op->ringRadius) - op->ringWidth / 2;
                        if (fromRing > op->ringPull) break;  // the hollow centre, far away
                        d = std::max(0.0f, fromRing);
                        if (fromRing > 0 && radial > 1e-6f) {
                            const Vec3 towards = across * ((radial > op->ringRadius ? -1.0f : 1.0f) / radial);
                            p.velocity = p.velocity + towards * (op->ringForce * dt * w);
                        }
                    }
                    const float speed = op->byDistance(d);
                    if (radial > 1e-4f)
                        p.position = c + axis * along + rotateAbout(across, axis, speed * dt * w / radial);
                    break;
                }
                case Operator::Kind::Remap: {
                    const Remap& rm = *op->remap;
                    float in = f;
                    if (rm.input == Remap::Input::SystemTime) in = float(time_);
                    else if (rm.input == Remap::Input::Distance)
                        in = length(p.position - points_[size_t(rm.inputPoint)].sim);
                    else if (rm.input == Remap::Input::Other)
                        in = 0;
                    const float x = rm.map(in, hashed(p.seed, op->index, 7) * 64, noiseSeed_ + 0x2000u + op->index);
                    const Vec3 y = rm.value(x);
                    switch (rm.output) {
                        case Remap::Output::Velocity:
                            for (size_t c = 0; c < 3; c++)
                                p.velocity[c] = lerp(p.velocity[c], rm.combine(p.velocity[c], y[c]), w);
                            break;
                        case Remap::Output::Speed: {
                            const float len = length(p.velocity);
                            if (len > 1e-6f) {
                                // A negligible least speed keeps the direction.
                                const float target = std::max(1e-4f, rm.combine(len, y[0]));
                                p.velocity = p.velocity * (lerp(len, target, w) / len);
                            }
                            break;
                        }
                        case Remap::Output::Opacity: alphaMul = lerp(alphaMul, rm.combine(alphaMul, y[0]), w); break;
                        case Remap::Output::Size: sizeMul = lerp(sizeMul, rm.combine(sizeMul, y[0]), w); break;
                        default: break;
                    }
                    break;
                }
                case Operator::Kind::ReduceMovement: {
                    const float d = length(p.position - points_[size_t(op->point)].sim);
                    if (d < op->distanceOuter) p.velocity = p.velocity * std::exp(-op->byDistance(d) * dt * w);
                    break;
                }
                case Operator::Kind::MaintainDistance: {
                    const Vec3 c = points_[size_t(op->point)].sim;
                    const Vec3 target = c + normalized(p.position - c, {1, 0, 0}) * op->distance;
                    p.position = p.position + (target - p.position) * (1 - std::exp(-op->strength * dt * w));
                    break;
                }
                case Operator::Kind::MaintainBetween: {
                    const Point& a = points_[size_t(op->point)];
                    const Point& b = points_[size_t(op->pointEnd)];
                    if (p.linePlace < 0) {
                        const Vec3 d = b.sim - a.sim;
                        const float len2 = dot(d, d);
                        p.linePlace = len2 > 1e-6f ? std::clamp(dot(p.position - a.sim, d) / len2, 0.0f, 1.0f) : 0.0f;
                    }
                    const float u = p.linePlace;
                    const Vec3 now = a.sim + (b.sim - a.sim) * u;
                    const Vec3 before = a.previous + (b.previous - a.previous) * u;
                    p.position = p.position + (now - before) * w;
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
    // Age out; deaths place eventdeath children.
    std::vector<Vec3> died;
    if (!children_.empty())
        for (const auto& p : particles_)
            if (p.age >= p.lifetime) died.push_back(p.position);
    std::erase_if(particles_, [](const Particle& p) { return p.age >= p.lifetime; });
    // Rope trails keep `segments` positions per `length` seconds (section 6.1).
    if (rendering_.kind == ParticleRendering::Kind::RopeTrail) {
        const float interval = rendering_.trailLength / float(std::max(1, rendering_.segments));
        trailClock_ += dt;
        for (int guard = 0; interval > 0 && trailClock_ >= interval; guard++) {
            trailClock_ -= interval;
            for (auto& p : particles_) {
                p.trail.insert(p.trail.begin(), p.position);
                if (p.trail.size() > size_t(rendering_.segments)) p.trail.pop_back();
            }
            if (guard > 64) trailClock_ = 0;
        }
    }
    runChildren(dtIn, born, died);
}

void ParticleSystem::runChildren(double dt, const std::vector<uint32_t>& born,
                                 const std::vector<Vec3>& died) {
    // Section 9. Particles stay in id (spawn) order, so ids are found by
    // binary search.
    auto find = [this](uint32_t id) -> const Particle* {
        auto it = std::lower_bound(particles_.begin(), particles_.end(), id,
                                   [](const Particle& p, uint32_t v) { return p.id < v; });
        return it != particles_.end() && it->id == id ? &*it : nullptr;
    };
    for (auto& slotPtr : children_) {
        ChildSlot& slot = *slotPtr;
        using Type = ChildSlot::Type;
        auto create = [&](const Vec3& place, uint32_t follow) {
            if (slot.instances.size() >= slot.maxCount) return;
            if (slot.def.probability < 1 && uniform(rng_) >= float(slot.def.probability)) return;
            ChildSlot::Instance instance;
            instance.system.reset(new ParticleSystem(slot.file, splitmix(rng_), lookup_, this, depth_ + 1));
            instance.place = place;
            instance.follow = follow;
            slot.instances.push_back(std::move(instance));
        };
        if (slot.type == Type::Spawn || slot.type == Type::Follow)
            for (uint32_t id : born)
                if (const Particle* p = find(id)) create(p->position, slot.type == Type::Follow ? id : 0);
        if (slot.type == Type::Death)
            for (const Vec3& place : died) create(place, 0);
        for (auto& instance : slot.instances) {
            if (instance.follow) {
                if (const Particle* p = find(instance.follow)) {
                    instance.place = p->position;
                } else {
                    instance.follow = 0;  // its particle died: emit no more
                    instance.system->stopEmitting();
                }
            }
            const Mat4 at = slot.type == Type::Static ? model_
                            : worldSpace()            ? Mat4::translate(instance.place[0], instance.place[1],
                                                                        instance.place[2])
                                                      : model_ * Mat4::translate(instance.place[0], instance.place[1],
                                                                                 instance.place[2]);
            ParticleSystem& child = *instance.system;
            child.setTransform(at * slot.local);
            child.setFactors(factors_);
            if (hasPointer_) child.setPointer(pointer_);
            if (slot.def.flags & 1) {
                // Set control points: this system's particles, oldest first.
                size_t k = 0;
                for (size_t i = size_t(std::clamp<int64_t>(slot.def.controlPointStart, 0, 7)); i < 8; i++) {
                    Point& point = child.points_[i];
                    point.fromParticle = k < particles_.size();
                    if (point.fromParticle) point.particle = toScene(particles_[k++].position);
                }
            }
            if (!instance.warmed) {
                instance.warmed = true;
                child.prewarm();
            }
            child.step(dt);
        }
        if (slot.type != Type::Static)
            std::erase_if(slot.instances, [](const ChildSlot::Instance& i) { return i.system->finished(); });
    }
}

void ParticleSystem::prewarm() {
    const double step = 1.0 / 30;
    for (double t = 0; t < file_.startTime; t += step) {
        // The rate factor applies inside step(); prewarm runs scene time.
        this->step(step);
    }
}

}  // namespace canvas::scene
