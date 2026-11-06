// Puppet MDL files, per src/scene/puppet/README.md section 2: the mesh
// (four known vertex formats), the skeleton with rest matrices and bone
// physics settings, attachments and animations.
//
// MdlFile::parse reads untrusted bytes with bounds checks. Sections after
// the mesh are found by their tag text; a damaged skeleton keeps the bones
// read so far and drops every animation, a damaged animation or attachment
// is skipped alone, and an unknown vertex format or damaged mesh fails the
// whole file (section 2.6). Notes describe what was skipped. No GPU.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "scene/model/Field.h"
#include "scene/render/Layout.h"

namespace canvas::scene {

struct PuppetMesh {
    std::vector<Vec3> positions;  // pixels, image centred, Y up
    std::vector<Vec2> uvs;        // image relative
    std::vector<std::array<uint32_t, 4>> bones;  // empty without a skeleton
    std::vector<Vec4> weights;
    std::vector<uint16_t> indices;  // three per triangle
};

// Bone physics settings (section 6; key names are an inference).
struct BonePhysics {
    bool rotation = false, translation = false, spring = true;
    float rotStiffness = 200, rotFriction = 20, rotInertia = 30;
    float moveStiffness = 200, moveFriction = 20, moveInertia = 30;
    bool limitAngle = false;
    float minAngle = -3.14159265f, maxAngle = 3.14159265f;
    bool limitDistance = false;
    float maxDistance = 100;
    float mass = 20, tipSize = 0;
    Vec2 tipDirection{1, 0};
    bool gravity = false;
    Vec2 gravityDirection{0, -1};
    bool ik = false;
    bool simulated() const { return rotation || translation; }
};

struct PuppetBone {
    std::string name;
    int parent = -1;  // always a smaller index, or -1
    Mat4 rest = Mat4::identity();  // relative to the parent
    // The rest matrix as 2D affine parts (section 2.2).
    Vec2 restPosition{0, 0};
    float restZ = 0;
    float restAngle = 0;
    Vec2 restScale{1, 1};
    BonePhysics physics;
};

struct PuppetAttachment {
    std::string name;
    int bone = 0;
    Mat4 matrix = Mat4::identity();  // relative to the bone
};

enum class PuppetPlayMode { Loop, Mirror, Single };

// One keyframe: parent relative position, Euler angles (radians), scale.
struct PuppetKey {
    Vec3 position{0, 0, 0};
    Vec3 angles{0, 0, 0};
    Vec3 scale{1, 1, 1};
};

struct PuppetAnimation {
    int32_t id = 0;
    std::string name;
    PuppetPlayMode mode = PuppetPlayMode::Loop;
    float fps = 30;
    uint32_t length = 0;  // frames; every track has length + 1 keys
    std::vector<PuppetKey> keys;  // bone b's key k at b * (length + 1) + k
    const PuppetKey& key(size_t bone, uint32_t frame) const { return keys[bone * (length + 1) + frame]; }
};

struct MdlFile {
    int version = 0;           // of the MDLV section
    uint32_t vertexFormat = 0;
    std::string material;
    PuppetMesh mesh;
    std::vector<PuppetBone> bones;
    std::vector<PuppetAttachment> attachments;
    std::vector<PuppetAnimation> animations;
    bool blendShapes = false;  // an MDMP section (section 2.5)
    bool ik = false;           // a bone with inverse kinematics
    std::vector<std::string> notes;  // what was skipped and why

    // Empty error on success; a mesh that cannot be read is an error.
    static MdlFile parse(std::span<const uint8_t> data, std::string& error);
    int boneIndex(const std::string& name) const;
    const PuppetAnimation* animation(int32_t id) const;
    int attachmentIndex(const std::string& name) const;
};

// The physics JSON text of one bone (empty: defaults, not simulated).
BonePhysics parseBonePhysics(const std::string& json);

}  // namespace canvas::scene
