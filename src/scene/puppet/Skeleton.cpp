// Implementation of PuppetRig declared in Skeleton.h
// (src/scene/puppet/README.md sections 3, 4 and 6).
#include "scene/puppet/Skeleton.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace canvas::scene {

namespace {

constexpr float kStep = 1.0f / 60.0f;
constexpr int kMaxSteps = 8;
constexpr float kGravity = 9.8f * 60.0f;   // pixels/s², section 6
constexpr float kMaxTipAcceleration = 1e5f;  // pixels/s²: ignores jumps
constexpr float kDefaultTip = 32.0f;          // no child, no tip size
constexpr float kPi = 3.14159265358979f;

Mat4 makeLocal(const Vec2& p, float z, float angle, const Vec2& s) {
    return Mat4::translate(p[0], p[1], z) * Mat4::rotateZ(angle) * Mat4::scale(s[0], s[1], 1);
}

float cross(const Vec2& a, const Vec2& b) { return a[0] * b[1] - a[1] * b[0]; }

Vec2 normalized(const Vec2& v, const Vec2& fallback) {
    float l = std::hypot(v[0], v[1]);
    return l > 1e-6f ? Vec2{v[0] / l, v[1] / l} : fallback;
}

Vec2 transformVector(const Mat4& m, const Vec2& v) {
    return {m.m[0] * v[0] + m.m[4] * v[1], m.m[1] * v[0] + m.m[5] * v[1]};
}

Vec2 transformPoint(const Mat4& m, const Vec2& v) {
    return {m.m[0] * v[0] + m.m[4] * v[1] + m.m[12], m.m[1] * v[0] + m.m[5] * v[1] + m.m[13]};
}

}  // namespace

PuppetRig::PuppetRig(std::shared_ptr<const MdlFile> file) : file_(std::move(file)) {
    const size_t n = file_->bones.size();
    local_.resize(n);
    world_.assign(n, Mat4::identity());
    inverseRestWorld_.assign(n, Mat4::identity());
    skin_.assign(n, Mat4::identity());
    worldOverride_.resize(n);
    localOverride_.resize(n);
    physics_.resize(n);
    onlyChild_.assign(n, -1);
    std::vector<int> children(n, 0);
    for (size_t b = 0; b < n; b++) {
        int p = file_->bones[b].parent;
        if (p < 0) continue;
        children[size_t(p)]++;
        onlyChild_[size_t(p)] = int(b);
    }
    for (size_t b = 0; b < n; b++)
        if (children[b] != 1) onlyChild_[b] = -1;
    restPose();
    for (size_t b = 0; b < n; b++) inverseRestWorld_[b] = world_[b].inverse();
}

void PuppetRig::restPose() {
    for (size_t b = 0; b < local_.size(); b++) {
        const PuppetBone& bone = file_->bones[b];
        local_[b] = {bone.restPosition, bone.restZ, bone.restAngle, bone.restScale};
        const Mat4 l = makeLocal(bone.restPosition, bone.restZ, bone.restAngle, bone.restScale);
        world_[b] = bone.parent >= 0 ? world_[size_t(bone.parent)] * l : l;
    }
}

uint64_t PuppetRig::addLayer(PuppetLayer layer) {
    layer.uid = nextUid_++;
    layers_.push_back(std::move(layer));
    return layers_.back().uid;
}

PuppetLayer* PuppetRig::layerByUid(uint64_t uid) {
    for (auto& l : layers_)
        if (l.uid == uid) return &l;
    return nullptr;
}

bool PuppetRig::removeLayer(uint64_t uid) {
    for (auto it = layers_.begin(); it != layers_.end(); ++it)
        if (it->uid == uid) {
            layers_.erase(it);
            return true;
        }
    return false;
}

const PuppetAnimation* PuppetRig::findAnimation(const std::string& key) const {
    for (const auto& a : file_->animations)
        if (a.name == key) return &a;
    char* end = nullptr;
    const long id = std::strtol(key.c_str(), &end, 10);
    if (!key.empty() && end && *end == 0) return file_->animation(int32_t(id));
    return nullptr;
}

double PuppetRig::frameOf(const PuppetLayer& layer) const {
    const PuppetAnimation* a = file_->animation(layer.animation);
    if (!a || a->length == 0) return 0;
    const double length = a->length;
    const double f = std::max(0.0, layer.time * a->fps);
    switch (a->mode) {
        case PuppetPlayMode::Single: return std::min(f, length);
        case PuppetPlayMode::Mirror: {
            double g = std::fmod(f, 2 * length);
            return g > length ? 2 * length - g : g;
        }
        case PuppetPlayMode::Loop: break;
    }
    return std::fmod(f, length);
}

void PuppetRig::setFrame(PuppetLayer& layer, double frame) const {
    const PuppetAnimation* a = file_->animation(layer.animation);
    if (!a || !(a->fps > 0) || !std::isfinite(frame)) return;
    layer.time = std::clamp(frame, 0.0, double(a->length)) / a->fps;
    layer.finished = false;
}

void PuppetRig::advance(double dt) {
    if (!(dt > 0) || !std::isfinite(dt)) dt = 0;
    for (PuppetLayer& layer : layers_) {
        // Fades (section 3.2).
        const bool fadeIn = layer.blendIn && layer.blendTime > 0;
        const bool fadeOut = layer.blendOut && layer.blendTime > 0;
        if (layer.visible && !layer.wasVisible) layer.fade = fadeIn ? 0.0f : 1.0f;
        if (layer.visible) {
            layer.fade = fadeIn ? std::min(1.0f, layer.fade + float(dt) / layer.blendTime) : 1.0f;
        } else {
            layer.fade = fadeOut ? std::max(0.0f, layer.fade - float(dt) / layer.blendTime) : 0.0f;
        }
        layer.wasVisible = layer.visible;
        // Clock and end events (section 3.1, section 7).
        const PuppetAnimation* a = file_->animation(layer.animation);
        if (!a || !layer.playing || layer.finished || (!layer.visible && layer.fade <= 0)) continue;
        const double before = layer.time * a->fps;
        layer.time += dt * layer.rate;
        if (layer.time < 0) layer.time = 0;
        const double after = layer.time * a->fps;
        const double length = a->length;
        if (length <= 0) continue;
        if (a->mode == PuppetPlayMode::Single) {
            if (before < length && after >= length) {
                layer.ended++;
                if (layer.oneShot) layer.finished = true;
            }
        } else {
            const double period = a->mode == PuppetPlayMode::Mirror ? 2 * length : length;
            const double wraps = std::floor(after / period) - std::floor(before / period);
            if (wraps > 0) {
                layer.ended += uint64_t(wraps);
                if (layer.oneShot) layer.finished = true;
            }
        }
    }
}

void PuppetRig::setWorldOverride(int bone, const Mat4& world) {
    if (bone >= 0 && size_t(bone) < worldOverride_.size()) worldOverride_[size_t(bone)] = world;
}

void PuppetRig::setLocalOverride(int bone, const Vec2& position, float angle, const Vec2& scale) {
    if (bone < 0 || size_t(bone) >= localOverride_.size()) return;
    const float z = local_[size_t(bone)].z;
    localOverride_[size_t(bone)] = Pose{position, z, angle, scale};
}

void PuppetRig::addImpulse(int bone, const Vec2& directional, float angular) {
    if (bone < 0 || size_t(bone) >= physics_.size()) return;
    Physics& s = physics_[size_t(bone)];
    s.velocity = {s.velocity[0] + directional[0], s.velocity[1] + directional[1]};
    s.angularVelocity += angular;
}

void PuppetRig::resetPhysics(int bone) {
    for (size_t b = 0; b < physics_.size(); b++)
        if (bone < 0 || size_t(bone) == b) physics_[b] = Physics{};
}

float PuppetRig::tipLength(size_t bone) const {
    const BonePhysics& cfg = file_->bones[bone].physics;
    if (cfg.tipSize > 0) return cfg.tipSize;
    if (onlyChild_[bone] >= 0) {
        const Vec2& p = file_->bones[size_t(onlyChild_[bone])].restPosition;
        float l = std::hypot(p[0], p[1]);
        if (l > 1) return l;
    }
    return kDefaultTip;
}

// Section 6. pose is the animated local pose; the simulated offsets are
// added to it.
void PuppetRig::stepPhysics(size_t bone, const Mat4& parentWorld, Pose& pose, float dt) {
    const BonePhysics& cfg = file_->bones[bone].physics;
    Physics& s = physics_[bone];
    const Mat4 w = parentWorld * makeLocal(pose.position, pose.z, pose.angle, pose.scale);
    const float length = tipLength(bone);
    const Vec2 dir = normalized(cfg.tipDirection, {1, 0});
    const Vec2 tip = transformPoint(w, {dir[0] * length, dir[1] * length});
    const Vec2 origin{w.m[12], w.m[13]};
    const Vec2 r{tip[0] - origin[0], tip[1] - origin[1]};

    Vec2 accel{0, 0};
    if (dt > 0) {
        const Vec2 v{(tip[0] - s.lastTip[0]) / dt, (tip[1] - s.lastTip[1]) / dt};
        if (s.tipSamples >= 2) accel = {(v[0] - s.lastTipVelocity[0]) / dt, (v[1] - s.lastTipVelocity[1]) / dt};
        if (s.tipSamples >= 1) s.lastTipVelocity = v;
        s.tipSamples = std::min(s.tipSamples + 1, 2);
    }
    s.lastTip = tip;
    const float a = std::hypot(accel[0], accel[1]);
    if (!std::isfinite(a) || a > kMaxTipAcceleration) accel = {0, 0};
    const Vec2 gravity = cfg.gravity ? Vec2{normalized(cfg.gravityDirection, {0, -1})[0] * kGravity,
                                            normalized(cfg.gravityDirection, {0, -1})[1] * kGravity}
                                     : Vec2{0, 0};

    int steps = 0;
    if (dt > 0) steps = std::clamp(int(std::lround(dt / kStep)), 1, kMaxSteps);

    if (cfg.rotation) {
        const float inertia = 1.0f / (1.0f + cfg.rotInertia / 10.0f);
        const Vec2 push{-accel[0] * inertia + gravity[0], -accel[1] * inertia + gravity[1]};
        const float r2 = std::max(r[0] * r[0] + r[1] * r[1], 1.0f);
        const float drive = cross(r, push) / r2;
        const float k = cfg.spring ? cfg.rotStiffness / cfg.mass : 0.0f;
        const float c = cfg.rotFriction / cfg.mass;
        const float lo = cfg.limitAngle ? std::max(cfg.minAngle, -kPi) : -kPi;
        const float hi = cfg.limitAngle ? std::min(cfg.maxAngle, kPi) : kPi;
        for (int i = 0; i < steps; i++) {
            const float acc = drive - k * s.angle - c * s.angularVelocity;
            s.angularVelocity += acc * kStep;
            s.angle += s.angularVelocity * kStep;
            if (s.angle < lo || s.angle > hi) {
                s.angle = std::clamp(s.angle, lo, hi);
                s.angularVelocity = 0;
            }
        }
        if (!std::isfinite(s.angle) || !std::isfinite(s.angularVelocity)) s.angle = s.angularVelocity = 0;
        pose.angle += s.angle;
    }
    if (cfg.translation) {
        const float inertia = 1.0f / (1.0f + cfg.moveInertia / 10.0f);
        const Vec2 pushWorld{-accel[0] * inertia + gravity[0], -accel[1] * inertia + gravity[1]};
        const Vec2 push = transformVector(parentWorld.inverse(), pushWorld);
        const float k = cfg.spring ? cfg.moveStiffness / cfg.mass : 0.0f;
        const float c = cfg.moveFriction / cfg.mass;
        const float limit = cfg.limitDistance ? cfg.maxDistance : 4 * length;
        for (int i = 0; i < steps; i++) {
            for (size_t ax = 0; ax < 2; ax++) {
                const float acc = push[ax] - k * s.offset[ax] - c * s.velocity[ax];
                s.velocity[ax] += acc * kStep;
                s.offset[ax] += s.velocity[ax] * kStep;
            }
            const float d = std::hypot(s.offset[0], s.offset[1]);
            if (d > limit && d > 0) {
                s.offset = {s.offset[0] * limit / d, s.offset[1] * limit / d};
                s.velocity = {0, 0};
            }
        }
        if (!std::isfinite(s.offset[0] + s.offset[1] + s.velocity[0] + s.velocity[1]))
            s.offset = s.velocity = {0, 0};
        pose.position = {pose.position[0] + s.offset[0], pose.position[1] + s.offset[1]};
    }
}

void PuppetRig::evaluate(double dt) {
    if (!(dt > 0) || !std::isfinite(dt)) dt = 0;
    const size_t n = local_.size();
    // Start at rest (section 3.2).
    std::vector<Pose> pose(n);
    for (size_t b = 0; b < n; b++) {
        const PuppetBone& bone = file_->bones[b];
        pose[b] = {bone.restPosition, bone.restZ, bone.restAngle, bone.restScale};
    }
    for (const PuppetLayer& layer : layers_) {
        const float weight = layer.blend * layer.fade;
        if (!(weight > 0) || (!layer.visible && layer.fade <= 0) || layer.finished) continue;
        const PuppetAnimation* a = file_->animation(layer.animation);
        if (!a || a->length == 0) continue;
        const double f = frameOf(layer);
        const uint32_t k0 = std::min(uint32_t(std::floor(f)), a->length);
        const uint32_t k1 = std::min(k0 + 1, a->length);
        const float t = float(f - std::floor(f));
        for (size_t b = 0; b < n; b++) {
            const PuppetKey& p = a->key(b, k0);
            const PuppetKey& q = a->key(b, k1);
            const float kx = p.position[0] + (q.position[0] - p.position[0]) * t;
            const float ky = p.position[1] + (q.position[1] - p.position[1]) * t;
            const float ka = p.angles[2] + (q.angles[2] - p.angles[2]) * t;
            const float sx = p.scale[0] + (q.scale[0] - p.scale[0]) * t;
            const float sy = p.scale[1] + (q.scale[1] - p.scale[1]) * t;
            Pose& o = pose[b];
            const PuppetBone& bone = file_->bones[b];
            if (layer.additive) {
                o.position[0] += (kx - bone.restPosition[0]) * weight;
                o.position[1] += (ky - bone.restPosition[1]) * weight;
                o.angle += (ka - bone.restAngle) * weight;
                auto ratio = [&](float key, float rest) {
                    return std::fabs(rest) > 1e-6f ? 1.0f + (key / rest - 1.0f) * weight : 1.0f;
                };
                o.scale[0] *= ratio(sx, bone.restScale[0]);
                o.scale[1] *= ratio(sy, bone.restScale[1]);
            } else {
                o.position[0] += (kx - o.position[0]) * weight;
                o.position[1] += (ky - o.position[1]) * weight;
                o.angle += (ka - o.angle) * weight;
                o.scale[0] += (sx - o.scale[0]) * weight;
                o.scale[1] += (sy - o.scale[1]) * weight;
            }
        }
    }
    // Bones, parents first (section 3.3).
    for (size_t b = 0; b < n; b++) {
        const PuppetBone& bone = file_->bones[b];
        const Mat4 parentWorld = bone.parent >= 0 ? world_[size_t(bone.parent)] : Mat4::identity();
        Pose& p = pose[b];
        if (localOverride_[b]) p = *localOverride_[b];
        if (bone.physics.simulated() && !worldOverride_[b]) stepPhysics(b, parentWorld, p, float(dt));
        if (worldOverride_[b]) {
            world_[b] = *worldOverride_[b];
            const Mat4 l = parentWorld.inverse() * world_[b];
            p.position = {l.m[12], l.m[13]};
            p.z = l.m[14];
            p.angle = std::atan2(l.m[1], l.m[0]);
            p.scale = {std::hypot(l.m[0], l.m[1]), std::hypot(l.m[4], l.m[5])};
        } else {
            world_[b] = parentWorld * makeLocal(p.position, p.z, p.angle, p.scale);
        }
        local_[b] = p;
        skin_[b] = world_[b] * inverseRestWorld_[b];
        worldOverride_[b].reset();
        localOverride_[b].reset();
    }
    // One-shot layers that ended go away (section 7).
    layers_.erase(std::remove_if(layers_.begin(), layers_.end(),
                                 [](const PuppetLayer& l) { return l.oneShot && l.finished; }),
                  layers_.end());
    evaluated_ = true;
}

Mat4 PuppetRig::localMatrix(int bone) const {
    const Pose& p = local_[size_t(bone)];
    return makeLocal(p.position, p.z, p.angle, p.scale);
}

void PuppetRig::skin(std::vector<Vec3>& out) const {
    const PuppetMesh& m = file_->mesh;
    out.resize(m.positions.size());
    const bool weighted = !m.bones.empty() && !skin_.empty();
    for (size_t v = 0; v < m.positions.size(); v++) {
        const Vec3& rest = m.positions[v];
        if (!weighted) {
            out[v] = rest;
            continue;
        }
        float x = 0, y = 0, total = 0;
        for (size_t k = 0; k < 4; k++) {
            const float wgt = m.weights[v][k];
            const uint32_t b = m.bones[v][k];
            if (wgt == 0 || b >= skin_.size()) continue;
            const Mat4& s = skin_[b];
            x += wgt * (s.m[0] * rest[0] + s.m[4] * rest[1] + s.m[8] * rest[2] + s.m[12]);
            y += wgt * (s.m[1] * rest[0] + s.m[5] * rest[1] + s.m[9] * rest[2] + s.m[13]);
            total += wgt;
        }
        out[v] = total > 0 ? Vec3{x, y, rest[2]} : rest;
    }
}

Mat4 PuppetRig::attachmentFrame(int attachment) const {
    if (attachment < 0 || size_t(attachment) >= file_->attachments.size()) return Mat4::identity();
    const PuppetAttachment& a = file_->attachments[size_t(attachment)];
    if (size_t(a.bone) >= world_.size()) return Mat4::identity();
    return world_[size_t(a.bone)] * a.matrix;
}

}  // namespace canvas::scene
