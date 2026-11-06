// SceneScript, per src/scene/script/README.md: QuickJS-ng running the
// scripts of a loaded scene's fields.
//
// create() evaluates Wallpaper Engine's base classes (assets
// scripts/jsclasses/baseclasses.js), this project's globals (engine,
// thisScene, input, console, localStorage, shared, layer objects) and
// every script as an ES module with its script properties; a script that
// fails is reported and skipped. update() runs one frame: resizeScreen,
// live property changes, cursor events, init, applyUserProperties and
// update calls with their values written back to the fields, timers.
// Layer objects read and write the scene model's fields directly; what
// lies outside the model (measured sizes, particle playback) comes
// through ScriptHost, and timeline animations through the Animator. Not
// thread safe; one engine per scene.
//
// Without QuickJS at build time (CANVAS_SCENE_HAVE_QUICKJS unset),
// available() is false and create() fails.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/AudioSpectrum.h"
#include "scene/assets/AssetLocator.h"
#include "scene/model/SceneLoader.h"

namespace canvas::scene {

class Animator;

// One frame's inputs (section 3 and 5 of the spec).
struct ScriptFrame {
    double frameTime = 0, runtime = 0;  // seconds
    float timeOfDay = 0;                // 0..1, local time
    Vec2 screen{1920, 1080};            // output pixels
    Vec3 cursorWorld{0, 0, 0};          // scene units
    Vec2 cursorScreen{0, 0};            // output pixels
    bool cursorLeftDown = false;
    int64_t hovered = -1;           // object index under the cursor, -1 none
    Vec3 cursorLocal{0, 0, 0};      // the cursor in that layer's space
    // The audio spectrum for registered audio buffers
    // (src/scene/audio/README.md section 1); null reads silence.
    const canvas::AudioSpectrum* audio = nullptr;
};

// What scripts reach beyond the scene model; the renderer implements it.
class ScriptHost {
public:
    virtual ~ScriptHost() = default;
    // A layer's size in scene units: images their size, text the measured
    // box; zero for others.
    virtual Vec2 layerSize(size_t object) = 0;
    // Particle playback: 0 play, 1 pause, 2 stop.
    virtual void particleCommand(size_t object, int command) = 0;
    virtual bool particlePlaying(size_t object) = 0;
    // Sound playback (ISoundLayer): 0 play, 1 pause, 2 stop.
    virtual void soundCommand(size_t /*object*/, int /*command*/) {}
    virtual bool soundPlaying(size_t /*object*/) { return false; }
};

class ScriptEngine {
public:
    struct Options {
        std::string storageFile;            // localStorage JSON; empty: not kept
        size_t memoryLimit = size_t(64) << 20;
        size_t stackLimit = size_t(1) << 20;
        double callBudget = 0.1;            // seconds one call may run
    };

    static bool available();
    // Null with error when QuickJS is missing or the base classes fail to
    // load. The scene, assets, animator and host must outlive the engine.
    static std::unique_ptr<ScriptEngine> create(LoadedScene& scene, const AssetLocator& assets,
                                                Animator* animator, ScriptHost& host, Options options,
                                                std::string& error);
    ~ScriptEngine();
    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

    size_t scriptCount() const;  // scripts loaded and still running
    size_t failedCount() const;  // scripts that failed (reported once)
    // True once a script registered audio buffers.
    bool usesAudio() const;
    void update(const ScriptFrame& frame);
    // Live user property changes: re-sent script properties and
    // applyUserProperties(changed) on the next update.
    void userPropertiesChanged(const std::vector<std::string>& names);
    // Console lines since the last call.
    std::vector<std::string> takeLog();
    // Failures since the last call: scripts that stopped (load errors,
    // exceptions, calls over budget), script property and timer errors.
    std::vector<std::string> takeProblems();

    struct Impl;

private:
    explicit ScriptEngine(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace canvas::scene
