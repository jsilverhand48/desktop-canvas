// Implementation of the timeline animations declared in Animation.h.
#include "scene/render/Animation.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <type_traits>

namespace canvas::scene {

namespace {

double number(const nlohmann::json& v, double fallback) {
    if (v.is_number()) return v.get<double>();
    if (v.is_boolean()) return v.get<bool>() ? 1.0 : 0.0;
    if (v.is_string()) {
        try {
            return std::stod(v.get<std::string>());
        } catch (...) {
        }
    }
    return fallback;
}

void readHandle(const nlohmann::json& h, bool& on, double& x, double& y) {
    if (!h.is_object()) return;
    if (auto e = h.find("enabled"); e != h.end() && e->is_boolean()) on = e->get<bool>();
    if (auto v = h.find("x"); v != h.end()) x = number(*v, x);
    if (auto v = h.find("y"); v != h.end()) y = number(*v, y);
}

// Point on a cubic Bezier for parameter s.
double bezier(double p0, double p1, double p2, double p3, double s) {
    const double u = 1 - s;
    return u * u * u * p0 + 3 * u * u * s * p1 + 3 * u * s * s * p2 + s * s * s * p3;
}

// The segment from a to b (b may be a shifted copy, for wraploop).
double segment(const Keyframe& a, const Keyframe& b, double frame) {
    const double length = b.frame - a.frame;
    if (!(length > 0)) return b.value;
    const double x0 = a.frame, x3 = b.frame;
    const double x1 = a.frontOn ? a.frame + std::clamp(a.frontX, 0.0, 1.0) * length / 3 : x0;
    const double x2 = b.backOn ? b.frame + std::clamp(b.backX, -1.0, 0.0) * length / 3 : x3;
    const double y0 = a.value, y3 = b.value;
    const double y1 = a.frontOn ? a.value + a.frontY : y0;
    const double y2 = b.backOn ? b.value + b.backY : y3;
    // x(s) is monotonic while the control points stay inside the segment:
    // bisection finds s for the frame.
    double lo = 0, hi = 1;
    for (int i = 0; i < 40; i++) {
        double mid = (lo + hi) / 2;
        if (bezier(x0, x1, x2, x3, mid) < frame) lo = mid;
        else hi = mid;
    }
    return bezier(y0, y1, y2, y3, (lo + hi) / 2);
}

}  // namespace

std::optional<Timeline> parseTimeline(const nlohmann::json& animation) {
    if (!animation.is_object()) return std::nullopt;
    Timeline t;
    for (int c = 0; c < 4; c++) {
        auto it = animation.find("c" + std::to_string(c));
        if (it == animation.end() || !it->is_array()) break;
        std::vector<Keyframe> keys;
        for (const auto& k : *it) {
            if (!k.is_object()) continue;
            Keyframe f;
            if (auto v = k.find("frame"); v != k.end()) f.frame = number(*v, 0);
            if (auto v = k.find("value"); v != k.end()) f.value = number(*v, 0);
            if (auto v = k.find("back"); v != k.end()) readHandle(*v, f.backOn, f.backX, f.backY);
            if (auto v = k.find("front"); v != k.end()) readHandle(*v, f.frontOn, f.frontX, f.frontY);
            if (std::isfinite(f.frame) && std::isfinite(f.value)) keys.push_back(f);
        }
        std::stable_sort(keys.begin(), keys.end(),
                         [](const Keyframe& a, const Keyframe& b) { return a.frame < b.frame; });
        t.channels.push_back(std::move(keys));
    }
    if (t.channels.empty() || t.channels.front().empty()) return std::nullopt;
    if (auto o = animation.find("options"); o != animation.end() && o->is_object()) {
        if (auto v = o->find("fps"); v != o->end()) t.fps = number(*v, t.fps);
        if (auto v = o->find("length"); v != o->end()) t.length = number(*v, 0);
        if (auto v = o->find("mode"); v != o->end() && v->is_string()) {
            const std::string m = v->get<std::string>();
            t.mode = m == "loop" ? Timeline::Mode::Loop
                     : m == "mirror" ? Timeline::Mode::Mirror
                                     : Timeline::Mode::Single;
        }
        if (auto v = o->find("wraploop"); v != o->end() && v->is_boolean()) t.wrapLoop = v->get<bool>();
        if (auto v = o->find("startpaused"); v != o->end() && v->is_boolean())
            t.startPaused = v->get<bool>();
        if (auto v = o->find("name"); v != o->end() && v->is_string()) t.name = v->get<std::string>();
    }
    if (auto v = animation.find("relative"); v != animation.end() && v->is_boolean())
        t.relative = v->get<bool>();
    if (!(t.fps > 0) || !std::isfinite(t.fps)) t.fps = 30;
    if (!(t.length > 0) || !std::isfinite(t.length)) {
        // No length: the last keyframe of any channel ends the animation.
        t.length = 0;
        for (const auto& ch : t.channels)
            if (!ch.empty()) t.length = std::max(t.length, ch.back().frame);
    }
    return t;
}

double Timeline::framePosition(double t) const {
    double f = std::max(0.0, t * fps);
    if (!(length > 0)) return f;
    switch (mode) {
        case Mode::Single: return std::min(f, length);
        case Mode::Loop: return std::fmod(f, length);
        case Mode::Mirror: {
            double p = std::fmod(f, 2 * length);
            return p <= length ? p : 2 * length - p;
        }
    }
    return f;
}

double Timeline::channelAt(size_t channel, double frame) const {
    const auto& keys = channels.at(channel);
    if (keys.empty()) return 0;
    const bool wrap = mode == Mode::Loop && wrapLoop && length > keys.back().frame;
    if (frame >= keys.back().frame) {
        if (!wrap) return keys.back().value;
        Keyframe first = keys.front();
        first.frame += length;  // the first keyframe again, one loop later
        return segment(keys.back(), first, frame);
    }
    if (frame <= keys.front().frame) {
        if (!wrap || keys.front().frame <= 0) return keys.front().value;
        Keyframe first = keys.front();
        first.frame += length;
        return segment(keys.back(), first, frame + length);
    }
    auto next = std::upper_bound(keys.begin(), keys.end(), frame,
                                 [](double f, const Keyframe& k) { return f < k.frame; });
    return segment(*(next - 1), *next, frame);
}

std::vector<double> Timeline::evaluate(double t) const {
    const double f = framePosition(t);
    std::vector<double> out(channels.size());
    for (size_t c = 0; c < channels.size(); c++) out[c] = channelAt(c, f);
    return out;
}

template <class FieldT>
void Animator::addField(FieldT& field, int64_t owner) {
    if (!field.animated()) return;
    auto timeline = parseTimeline(field.dynamics->animation);
    if (!timeline) return;
    const size_t before = appliers_.size();
    using T = std::decay_t<decltype(field.value)>;
    auto* target = &field;
    const T base = field.value;
    if constexpr (std::is_same_v<T, float>) {
        appliers_.push_back([target, base, tl = *timeline](double t) {
            double v = tl.evaluate(t)[0];
            target->value = float(tl.relative ? base + v : v);
        });
    } else if constexpr (std::is_same_v<T, Vec2> || std::is_same_v<T, Vec3> ||
                         std::is_same_v<T, Vec4>) {
        appliers_.push_back([target, base, tl = *timeline](double t) {
            auto v = tl.evaluate(t);
            T out = base;
            for (size_t i = 0; i < out.size() && i < v.size(); i++)
                out[i] = float(tl.relative ? base[i] + v[i] : v[i]);
            target->value = out;
        });
    } else if constexpr (std::is_same_v<T, Floats>) {
        appliers_.push_back([target, base, tl = *timeline](double t) {
            auto v = tl.evaluate(t);
            Floats out = base;
            const int n = std::max<int>(base.count, int(std::min<size_t>(v.size(), 4)));
            for (int i = 0; i < n && size_t(i) < v.size(); i++)
                out.v[size_t(i)] = float(tl.relative ? base.v[size_t(i)] + v[size_t(i)]
                                                     : v[size_t(i)]);
            out.count = std::max(out.count, std::min<int>(int(v.size()), 4));
            target->value = out;
        });
    } else if constexpr (std::is_same_v<T, bool>) {
        appliers_.push_back([target, tl = *timeline](double t) {
            target->value = tl.evaluate(t)[0] >= 0.5;
        });
    }
    // Text and integer fields are not animated (none in the corpus).
    if (appliers_.size() == before) return;
    auto playback = std::make_unique<AnimationPlayback>();
    playback->timeline = *timeline;
    playback->object = owner;
    playback->playing = !timeline->startPaused;
    playbacks_.push_back(std::move(playback));
}

Animator::Animator(LoadedScene& scene) {
    // Objects first, so each animation knows its object.
    for (size_t i = 0; i < scene.scene.objects.size(); i++)
        scene.scene.objects[i].forEachField([&](auto& field) { addField(field, int64_t(i)); });
    auto visit = [&](auto& field) { addField(field, -1); };
    scene.scene.camera.forEachField(visit);
    scene.scene.general.forEachField(visit);
    for (auto& [path, material] : scene.materials)
        if (material) material->forEachField(visit);
}

void Animator::addObject(LoadedScene& scene, size_t index) {
    if (index >= scene.scene.objects.size()) return;
    scene.scene.objects[index].forEachField([&](auto& field) { addField(field, int64_t(index)); });
}

void Animator::update(double t) {
    const double dt = std::max(0.0, t - last_);
    last_ = t;
    for (size_t i = 0; i < appliers_.size(); i++) {
        AnimationPlayback& p = *playbacks_[i];
        if (p.playing) p.time += dt * p.rate;
        appliers_[i](p.time);
    }
}

AnimationPlayback* Animator::find(int64_t object, const std::string& name) {
    for (auto& p : playbacks_)
        if (p->object == object && (name.empty() || p->timeline.name == name)) return p.get();
    return nullptr;
}

}  // namespace canvas::scene
