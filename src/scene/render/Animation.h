// Timeline animations, per src/scene/render/README.md section 10.
//
// parseTimeline() reads the "animation" object of a {"value", "animation"}
// wrapper: channels c0, c1, ... of keyframes, the options (fps, length,
// mode, wraploop, startpaused) and `relative`. Timeline::evaluate(t) gives
// each channel's value at scene time t: the frame position follows the
// mode (single holds the end, loop wraps, mirror runs back and forth),
// and between keyframes the value follows a cubic Bezier whose handles
// sit at handle.x thirds of the segment (y in value units), solved for the
// frame. Animator finds every animated field of a loaded scene and writes
// the evaluated values into them each frame (relative ones on top of the
// value the field had at load). No GPU, deterministic.
#pragma once

#include <functional>
#include <optional>
#include <vector>

#include <nlohmann/json.hpp>

#include "scene/model/SceneLoader.h"

namespace canvas::scene {

struct Keyframe {
    double frame = 0;
    double value = 0;
    bool backOn = true, frontOn = true;
    double backX = -1, backY = 0, frontX = 1, frontY = 0;
};

struct Timeline {
    enum class Mode { Single, Loop, Mirror };

    std::vector<std::vector<Keyframe>> channels;  // c0, c1, ... sorted by frame
    double fps = 30;
    double length = 0;  // frames
    Mode mode = Mode::Single;
    bool wrapLoop = false;
    bool startPaused = false;
    bool relative = false;

    // The frame position at scene time t (seconds).
    double framePosition(double t) const;
    // Every channel's value at scene time t.
    std::vector<double> evaluate(double t) const;
    // One channel at a frame position.
    double channelAt(size_t channel, double frame) const;
};

// nullopt when the object has no usable channel.
std::optional<Timeline> parseTimeline(const nlohmann::json& animation);

class Animator {
public:
    // Finds the animated fields of `scene`, which must outlive the Animator
    // and keep its fields in place.
    explicit Animator(LoadedScene& scene);

    // Writes every animated field's value for scene time t.
    void update(double t);
    size_t size() const { return appliers_.size(); }

private:
    std::vector<std::function<void(double)>> appliers_;
};

}  // namespace canvas::scene
