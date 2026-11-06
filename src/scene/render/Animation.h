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
// value the field had at load). Each animation runs on its own clock, an
// AnimationPlayback that scripts may play, pause, stop, seek and speed
// (src/scene/script/README.md section 4); `startpaused` ones begin
// stopped on frame 0. No GPU, deterministic.
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
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

    std::string name;  // options.name, often empty

    std::vector<std::vector<Keyframe>> channels;  // c0, c1, ... sorted by frame
    double fps = 30;
    double length = 0;  // frames
    Mode mode = Mode::Single;
    bool wrapLoop = false;
    bool startPaused = false;
    bool relative = false;

    // The frame position t seconds into the animation.
    double framePosition(double t) const;
    // Every channel's value t seconds into the animation.
    std::vector<double> evaluate(double t) const;
    // One channel at a frame position.
    double channelAt(size_t channel, double frame) const;
};

// nullopt when the object has no usable channel.
std::optional<Timeline> parseTimeline(const nlohmann::json& animation);

// One animated field's clock (seconds into its timeline).
struct AnimationPlayback {
    Timeline timeline;
    int64_t object = -1;  // index of the owning scene object; -1 scene settings, materials
    bool playing = true;  // false while paused or stopped (startpaused begins so)
    double time = 0;
    double rate = 1;
    double frame() const { return timeline.framePosition(time); }
};

class Animator {
public:
    // Finds the animated fields of `scene`, which must outlive the Animator
    // and keep its fields in place.
    explicit Animator(LoadedScene& scene);

    // Advances the playing clocks to scene time t (by the time since the
    // last call, times their rates) and writes every animated field.
    void update(double t);
    size_t size() const { return appliers_.size(); }

    // An object's animations in scene file order; with a name, the first
    // of that name (null when none).
    AnimationPlayback* find(int64_t object, const std::string& name);
    // The animations of an object added after construction (a layer a
    // script created, src/scene/script/README.md section 3.1); they start
    // at the current scene time.
    void addObject(LoadedScene& scene, size_t index);

private:
    template <class FieldT> void addField(FieldT& field, int64_t owner);
    std::vector<std::unique_ptr<AnimationPlayback>> playbacks_;
    std::vector<std::function<void(double)>> appliers_;  // by playback index, given its time
    double last_ = 0;
};

}  // namespace canvas::scene
