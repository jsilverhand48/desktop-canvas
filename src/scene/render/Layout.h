// The GPU free part of rendering, per src/scene/render/README.md sections 1
// and 2: matrices, world transforms through parent chains [req A3],
// hierarchical visibility evaluated each frame [req A4], puppet attachment
// frames [req A6], layer rectangles with alignment, and the view
// projection with its output mapping.
//
// SceneLayout indexes a SceneFile's objects once and recomputes every
// object's world transform and visibility from the fields' current values
// in update(), so scripts, bindings and animations that change them are
// seen on the next frame. Matrices are column major in the GL convention
// (clip = M * v).
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "scene/model/SceneFile.h"

namespace canvas::scene {

struct Mat4 {
    std::array<float, 16> m{};  // column major: m[column * 4 + row]

    static Mat4 identity();
    static Mat4 translate(float x, float y, float z);
    static Mat4 scale(float x, float y, float z);
    static Mat4 rotateZ(float radians);
    // Maps x left..right, y bottom..top, z near..far to clip space.
    static Mat4 ortho(float left, float right, float bottom, float top, float near, float far);

    Mat4 operator*(const Mat4& o) const;
    Vec4 operator*(const Vec4& v) const;
    Mat4 inverse() const;  // general 4x4; identity when singular
    float at(int row, int column) const { return m[size_t(column) * 4 + size_t(row)]; }
};

// An object's placement after its parents [req A3].
struct WorldTransform {
    Vec3 origin{0, 0, 0};
    Vec3 scale{1, 1, 1};
    float angle = 0;  // radians about Z
    Mat4 matrix() const;  // translate(origin) * rotateZ(angle) * scale(scale)
};

class SceneLayout {
public:
    static constexpr int kMaxParentDepth = 32;

    explicit SceneLayout(const SceneFile& scene);

    // Recomputes transforms and visibility from the current field values.
    void update();

    // Attachment frames (src/scene/puppet/README.md section 4): for an
    // object whose `attachment` names a frame of its parent, the frame in
    // the parent's own coordinates; false for no such frame (plain
    // parenting).
    using AttachmentFrames =
        std::function<bool(size_t parent, const std::string& attachment, Mat4& frame)>;
    void setAttachmentFrames(AttachmentFrames frames) { attachments_ = std::move(frames); }

    const WorldTransform& world(size_t object) const { return world_[object]; }
    bool visible(size_t object) const { return visible_[object] != 0; }
    // Index of the object's parent, or -1.
    int parent(size_t object) const { return parent_[object]; }
    size_t size() const { return world_.size(); }

private:
    const SceneFile& scene_;
    AttachmentFrames attachments_;
    std::vector<int> parent_;
    std::vector<WorldTransform> world_;
    std::vector<uint8_t> visible_;
};

// A rectangle in an object's own coordinates.
struct LayerRect {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // left, bottom, right, top
};

// The rectangle of a size with the origin at the named point (section 2):
// center (also empty or unknown), left, right, top, bottom, topleft,
// topright, bottomleft, bottomright.
LayerRect layerRect(Vec2 size, const std::string& alignment);

enum class Scaling { Fill, Fit, Stretch };
Scaling scalingFromName(const std::string& name);  // unknown: Fill

// Projection from scene coordinates to the clip space of a frame of
// width x height pixels, for a view of projectionWidth x projectionHeight
// (section 1).
Mat4 viewProjection(float projectionWidth, float projectionHeight, float zoom, int width,
                    int height, Scaling scaling);

}  // namespace canvas::scene
