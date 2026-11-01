// Draws a loaded scene with GL 3.3 core, per src/scene/render/README.md.
//
// load() builds everything a frame needs: one program per distinct shader
// request (translated, then compiled through ProgramCache, in parallel
// where the driver can), the textures the programs sample, and per layer
// its material passes, its effects' pass graphs with their named buffers,
// and the base pass Wallpaper Engine runs before effects
// (materials/util/effectpassthrough or effectcomposebackground from the
// assets folder). render() lays the scene out (SceneLayout, so parent
// transforms and visibility follow the fields' current values), draws
// image, solid, composition and fullscreen layers in order into an
// offscreen frame laid out top row first (section 1), running effects
// through pooled ping pong buffers, then presents the frame into the
// framebuffer that was bound when render() was called: fill and stretch
// cover it, fit centres the frame and fills the bars by the clamp mode.
//
// A frame is drawn once per advance(): rendering the same size again
// before the next advance() only presents it again, so outputs that share
// a pixel size share the work. Layers of kinds this step does not draw
// (particles, text, shapes, ...) are skipped, as are passes whose program
// does not compile and effects with such a pass; each problem is logged
// and listed once. Construction, load() and every other call need the GL
// context current.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "scene/assets/AssetLocator.h"
#include "scene/model/SceneLoader.h"
#include "scene/render/Layout.h"

namespace canvas::scene {

// How fit scaling fills the bars beside the frame: the edge pixels
// stretched (Clamp), the frame tiled (Repeat), or the clear colour (Border).
enum class Clamp { Clamp, Border, Repeat };
Clamp clampFromName(const std::string& name);  // unknown: Clamp

// Diagnostics (desktop-canvas-scene --render-debug, section 6).
struct RenderDebug {
    bool passLog = false;    // log every pass of the first frame drawn
    bool baseOnly = false;   // run no effects (and no bloom)
    bool noBloom = false;    // skip the bloom chain
    bool noParticles = false;  // --disable-particles: no particle layers
    // Experimental: hdr scenes in float buffers with the HDR bloom chain
    // (Wallpaper Engine's "Ultra" post-processing) instead of the normal
    // bloom every other user sees; section 5.
    bool ultra = false;
    bool keepAlpha = false;  // present the frame's alpha instead of opaque
    std::set<int64_t> only;  // draw only these objects (empty: all)
    std::set<int64_t> skip;  // never draw these objects
    // (object id, effect index) pairs to skip; index -1 skips them all.
    std::set<std::pair<int64_t, int>> skipEffects;
    // Writes every translated program here (<n>.vert, <n>.frag and
    // <n>.txt naming the pass and request); empty: off.
    std::filesystem::path dumpPrograms;

    // Parses one comma separated list (pass-log, base-only, no-bloom,
    // no-particles, ultra, no-solid-final, object=ID, skip-object=ID,
    // skip-effect=ID[:N], dump-programs=DIR);
    // false (with error) on an unknown item.
    bool parse(const std::string& list, std::string& error);
};

class SceneRenderer {
public:
    struct Options {
        Scaling scaling = Scaling::Fill;
        Clamp clamp = Clamp::Clamp;
        std::filesystem::path shaderCache;  // program binaries; empty: none
        RenderDebug debug;
    };

    SceneRenderer(const AssetLocator& assets, Options options);
    ~SceneRenderer();
    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    // False (with error) when the scene has no layer this step can draw.
    bool load(LoadedScene scene, std::string& error);

    void advance(double dt);
    void render(int width, int height);
    // Live user property change (--set-property text form).
    void setProperty(const std::string& name, const std::string& value);
    // Pointer position, 0..1 across the output, y down.
    void setPointer(float x, float y);

    const std::vector<std::string>& problems() const;
    size_t layerCount() const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace canvas::scene
