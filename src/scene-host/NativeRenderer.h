// The native scene engine (scene/gl/SceneRenderer) behind the helper's
// Renderer interface.
//
// create() opens the wallpaper's files and loads the scene, which needs no
// GL, so it runs before a context exists and before the sandbox. The GL
// side (textures, programs, buffers) is built at the first render(), when
// the host has made a context current; time advanced before that is kept.
// If the scene turns out to have nothing drawable, render() clears to the
// placeholder colour instead. Pointer positions arrive in output logical
// pixels and are normalised by the size of the last frame rendered (phase
// P4b keeps per output state).
//
// GL objects can only be deleted with their context current: shutdown()
// frees them and must run while it is. Without it the destructor leaves
// them to the driver, which frees them when the process exits.
#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "scene-host/Renderer.h"
#include "scene/assets/AssetLocator.h"
#include "scene/gl/SceneRenderer.h"
#include "scene/model/SceneLoader.h"

namespace canvas::scenehost {

class NativeRenderer : public Renderer {
public:
    struct Options {
        scene::Scaling scaling = scene::Scaling::Fill;
        scene::Clamp clamp = scene::Clamp::Clamp;
        std::filesystem::path shaderCache;  // empty: no disk cache
        float placeholder[3] = {0, 0, 0};   // shown when nothing draws
        scene::RenderDebug debug;
    };

    // nullptr (with error) when the wallpaper's files or its scene cannot
    // be read.
    static std::unique_ptr<NativeRenderer> create(const std::filesystem::path& wallpaper,
                                                  const std::filesystem::path& assetsDir,
                                                  const scene::PropertyValues& overrides,
                                                  Options options, std::string& error);
    ~NativeRenderer() override;

    void advance(double dt) override;
    void render(int width, int height) override;
    void setPointer(const std::string& output, double x, double y) override;
    void setProperty(const std::string& name, const std::string& value) override;

    // Problems the engine met so far (empty before the first render).
    std::vector<std::string> problems() const;

    // Frees the engine's GL objects; the context must be current.
    void shutdown();

private:
    explicit NativeRenderer(Options options) : options_(std::move(options)) {}
    void start();

    Options options_;
    scene::AssetLocator assets_;
    std::optional<scene::LoadedScene> pending_;  // until the first render
    std::unique_ptr<scene::SceneRenderer> renderer_;
    bool failed_ = false;
    double elapsed_ = 0;  // time advanced before the engine existed
    double pointer_[2] = {-1, -1};
    int lastWidth_ = 0, lastHeight_ = 0;
};

}  // namespace canvas::scenehost
