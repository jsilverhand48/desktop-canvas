// A posed puppet, per src/scene/puppet/README.md sections 3, 4 and 6:
// animation layers with their own clocks and fades, the bone pose
// (normal and additive blending, script overrides, bone physics), world
// and skin matrices, CPU skinning and attachment frames.
//
// Each frame: advance(dt) moves the layer clocks and fades, scripts may
// then set overrides, and evaluate(dt) builds the pose (overrides apply
// for that evaluation only, physics steps by dt at a fixed 1/60 s). Bone
// matrices are in puppet space: image centred, Y up, unscaled pixels.
// Deterministic, no GPU.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "scene/puppet/MdlFile.h"

namespace canvas::scene {

struct PuppetLayer {
    int32_t animation = 0;  // MDLA id
    std::string name;
    float rate = 1, blend = 1, blendTime = 0;
    bool visible = false, additive = false, blendIn = false, blendOut = false;
    bool oneShot = false;  // playSingleAnimation: removed after one run
    // Runtime state.
    uint64_t uid = 0;      // stable handle for scripts (set by addLayer)
    double time = 0;       // seconds on the layer's own clock
    bool playing = true;
    float fade = 1;
    bool wasVisible = false;
    bool finished = false;  // a one-shot layer that ended
    uint64_t ended = 0;     // times it reached its end (scripts' callbacks)
};

class PuppetRig {
public:
    explicit PuppetRig(std::shared_ptr<const MdlFile> file);

    const MdlFile& file() const { return *file_; }
    std::vector<PuppetLayer>& layers() { return layers_; }
    const std::vector<PuppetLayer>& layers() const { return layers_; }
    // Appends a layer with a fresh uid; returns the uid.
    uint64_t addLayer(PuppetLayer layer);
    PuppetLayer* layerByUid(uint64_t uid);
    bool removeLayer(uint64_t uid);
    // An animation by name (first match) or, for a number text, by id.
    const PuppetAnimation* findAnimation(const std::string& nameOrId) const;

    // Section 3.1: clocks, end events and fades.
    void advance(double dt);
    // Frame of a layer now (fractional); 0 for an unknown animation.
    double frameOf(const PuppetLayer& layer) const;
    void setFrame(PuppetLayer& layer, double frame) const;

    // Section 7: overrides for the next evaluate() (puppet space).
    void setWorldOverride(int bone, const Mat4& world);
    void setLocalOverride(int bone, const Vec2& position, float angle, const Vec2& scale);
    void addImpulse(int bone, const Vec2& directional, float angular);
    void resetPhysics(int bone);  // -1: every bone

    // Sections 3.2, 3.3 and 6.
    void evaluate(double dt);
    bool evaluated() const { return evaluated_; }

    size_t boneCount() const { return world_.size(); }
    const Mat4& world(int bone) const { return world_[size_t(bone)]; }
    // The bone's local pose of the last evaluation (parent relative).
    Vec2 localPosition(int bone) const { return local_[size_t(bone)].position; }
    float localAngle(int bone) const { return local_[size_t(bone)].angle; }
    Vec2 localScale(int bone) const { return local_[size_t(bone)].scale; }
    Mat4 localMatrix(int bone) const;
    // Section 3.4: skinned positions of every vertex (z kept).
    void skin(std::vector<Vec3>& out) const;
    // Section 4: bone world * attachment matrix (rest pose before the
    // first evaluation); identity for a bad index.
    Mat4 attachmentFrame(int attachment) const;

private:
    struct Pose {
        Vec2 position{0, 0};
        float z = 0;
        float angle = 0;
        Vec2 scale{1, 1};
    };
    struct Physics {
        float angle = 0, angularVelocity = 0;
        Vec2 offset{0, 0}, velocity{0, 0};
        Vec2 lastTip{0, 0}, lastTipVelocity{0, 0};
        int tipSamples = 0;
    };
    void restPose();
    void stepPhysics(size_t bone, const Mat4& parentWorld, Pose& pose, float step);
    float tipLength(size_t bone) const;

    std::shared_ptr<const MdlFile> file_;
    std::vector<PuppetLayer> layers_;
    std::vector<Pose> local_;
    std::vector<Mat4> world_, inverseRestWorld_, skin_;
    std::vector<std::optional<Mat4>> worldOverride_;
    std::vector<std::optional<Pose>> localOverride_;
    std::vector<Physics> physics_;
    std::vector<int> onlyChild_;  // the single child, -1 for none or several
    uint64_t nextUid_ = 1;
    bool evaluated_ = false;
};

}  // namespace canvas::scene
