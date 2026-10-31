// Implementation of the layout math declared in Layout.h.
#include "scene/render/Layout.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace canvas::scene {

Mat4 Mat4::identity() {
    Mat4 r;
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1;
    return r;
}

Mat4 Mat4::translate(float x, float y, float z) {
    Mat4 r = identity();
    r.m[12] = x;
    r.m[13] = y;
    r.m[14] = z;
    return r;
}

Mat4 Mat4::scale(float x, float y, float z) {
    Mat4 r;
    r.m[0] = x;
    r.m[5] = y;
    r.m[10] = z;
    r.m[15] = 1;
    return r;
}

Mat4 Mat4::rotateZ(float a) {
    Mat4 r = identity();
    float c = std::cos(a), s = std::sin(a);
    r.m[0] = c;
    r.m[1] = s;
    r.m[4] = -s;
    r.m[5] = c;
    return r;
}

Mat4 Mat4::ortho(float l, float r, float b, float t, float n, float f) {
    Mat4 o = identity();
    o.m[0] = 2 / (r - l);
    o.m[5] = 2 / (t - b);
    o.m[10] = -2 / (f - n);
    o.m[12] = -(r + l) / (r - l);
    o.m[13] = -(t + b) / (t - b);
    o.m[14] = -(f + n) / (f - n);
    return o;
}

Mat4 Mat4::operator*(const Mat4& o) const {
    Mat4 r;
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++) {
            float sum = 0;
            for (int k = 0; k < 4; k++) sum += at(row, k) * o.at(k, c);
            r.m[size_t(c) * 4 + size_t(row)] = sum;
        }
    return r;
}

Vec4 Mat4::operator*(const Vec4& v) const {
    Vec4 r{};
    for (int row = 0; row < 4; row++)
        r[size_t(row)] = at(row, 0) * v[0] + at(row, 1) * v[1] + at(row, 2) * v[2] + at(row, 3) * v[3];
    return r;
}

Mat4 Mat4::inverse() const {
    // Cofactor expansion (column major array, same formulas either way).
    const float* a = m.data();
    float inv[16];
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if (std::fabs(det) < 1e-12f) return identity();
    Mat4 r;
    for (int i = 0; i < 16; i++) r.m[size_t(i)] = inv[i] / det;
    return r;
}

Mat4 WorldTransform::matrix() const {
    return Mat4::translate(origin[0], origin[1], origin[2]) * Mat4::rotateZ(angle) *
           Mat4::scale(scale[0], scale[1], scale[2]);
}

SceneLayout::SceneLayout(const SceneFile& scene) : scene_(scene) {
    const size_t n = scene.objects.size();
    parent_.assign(n, -1);
    world_.resize(n);
    visible_.assign(n, 1);
    std::unordered_map<int64_t, int> byId;
    for (size_t i = 0; i < n; i++) byId.emplace(scene.objects[i].id, int(i));
    for (size_t i = 0; i < n; i++) {
        const auto& p = scene.objects[i].parent;
        if (!p) continue;
        auto it = byId.find(*p);
        if (it != byId.end() && it->second != int(i)) parent_[i] = it->second;
    }
    update();
}

void SceneLayout::update() {
    const auto& objects = scene_.objects;
    for (size_t i = 0; i < objects.size(); i++) {
        // Collect the chain, nearest parent first, at most 32 levels.
        int chain[kMaxParentDepth];
        int depth = 0;
        for (int p = parent_[i]; p >= 0 && depth < kMaxParentDepth; p = parent_[size_t(p)])
            chain[depth++] = p;
        const SceneObject& o = objects[i];
        WorldTransform w;
        w.origin = o.origin.value;
        w.scale = o.scale.value;
        w.angle = o.angles.value[2];
        bool shown = o.visible.value;
        for (int k = 0; k < depth; k++) {
            const SceneObject& parent = objects[size_t(chain[k])];
            const Vec3& ps = parent.scale.value;
            float pa = parent.angles.value[2];
            float x = w.origin[0] * ps[0], y = w.origin[1] * ps[1];
            float c = std::cos(pa), s = std::sin(pa);
            w.origin = {parent.origin.value[0] + c * x - s * y, parent.origin.value[1] + s * x + c * y,
                        parent.origin.value[2] + w.origin[2] * ps[2]};
            w.scale = {w.scale[0] * ps[0], w.scale[1] * ps[1], w.scale[2] * ps[2]};
            w.angle += pa;
            shown = shown && parent.visible.value;
        }
        world_[i] = w;
        visible_[i] = shown ? 1 : 0;
    }
}

LayerRect layerRect(Vec2 size, const std::string& alignment) {
    float w = size[0], h = size[1];
    LayerRect r{-w / 2, -h / 2, w / 2, h / 2};
    auto has = [&](const char* part) { return alignment.find(part) != std::string::npos; };
    if (has("left")) {
        r.x0 = 0;
        r.x1 = w;
    } else if (has("right")) {
        r.x0 = -w;
        r.x1 = 0;
    }
    if (has("top")) {
        r.y0 = -h;
        r.y1 = 0;
    } else if (has("bottom")) {
        r.y0 = 0;
        r.y1 = h;
    }
    return r;
}

Scaling scalingFromName(const std::string& name) {
    if (name == "fit") return Scaling::Fit;
    if (name == "stretch") return Scaling::Stretch;
    return Scaling::Fill;
}

Mat4 viewProjection(float pw, float ph, float zoom, int width, int height, Scaling scaling) {
    if (pw <= 0 || ph <= 0 || width <= 0 || height <= 0) return Mat4::identity();
    if (!(zoom > 0)) zoom = 1;
    const float cx = pw / 2, cy = ph / 2;
    if (scaling == Scaling::Stretch) {
        float hw = pw / (2 * zoom), hh = ph / (2 * zoom);
        return Mat4::ortho(cx - hw, cx + hw, cy - hh, cy + hh, -10000, 10000);
    }
    float sx = float(width) / pw, sy = float(height) / ph;
    float s = (scaling == Scaling::Fill ? std::max(sx, sy) : std::min(sx, sy)) * zoom;
    float hw = float(width) / (2 * s), hh = float(height) / (2 * s);
    return Mat4::ortho(cx - hw, cx + hw, cy - hh, cy + hh, -10000, 10000);
}

}  // namespace canvas::scene
