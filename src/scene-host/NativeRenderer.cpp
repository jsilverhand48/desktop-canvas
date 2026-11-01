// Implementation of NativeRenderer declared in NativeRenderer.h.
#include "scene-host/NativeRenderer.h"

#include <epoxy/gl.h>

#include "core/Log.h"

namespace canvas::scenehost {

std::unique_ptr<NativeRenderer> NativeRenderer::create(const std::filesystem::path& wallpaper,
                                                       const std::filesystem::path& assetsDir,
                                                       const scene::PropertyValues& overrides,
                                                       Options options, std::string& error) {
    std::unique_ptr<NativeRenderer> r(new NativeRenderer(std::move(options)));
    if (!r->assets_.open(wallpaper, assetsDir)) {
        error = r->assets_.error();
        return nullptr;
    }
    scene::LoadedScene loaded;
    if (!scene::loadScene(r->assets_, overrides, loaded)) {
        error = loaded.report.errors.empty() ? "the scene cannot be read"
                                             : loaded.report.errors.front();
        return nullptr;
    }
    r->pending_ = std::move(loaded);
    return r;
}

NativeRenderer::~NativeRenderer() {
    // No context may be current any more (see the header): leave the GL
    // objects to the driver instead of calling GL here.
    if (renderer_) (void)renderer_.release();
}

void NativeRenderer::shutdown() {
    renderer_.reset();
    failed_ = true;  // nothing left to draw with
}

void NativeRenderer::start() {
    scene::SceneRenderer::Options o;
    o.scaling = options_.scaling;
    o.clamp = options_.clamp;
    o.shaderCache = options_.shaderCache;
    o.scriptStorage = options_.scriptStorage;
    o.debug = options_.debug;
    renderer_ = std::make_unique<scene::SceneRenderer>(assets_, o);
    std::string error;
    if (!renderer_->load(std::move(*pending_), error)) {
        log::warn() << "native renderer: " << error;
        failed_ = true;
    }
    pending_.reset();
    if (elapsed_ > 0) renderer_->advance(elapsed_);
    log::info() << "native renderer: " << renderer_->layerCount() << " layers, "
                << renderer_->scriptCount() << " scripts, " << renderer_->problems().size() << " problems";
}

void NativeRenderer::advance(double dt) {
    if (renderer_) renderer_->advance(dt);
    else elapsed_ += dt;
}

void NativeRenderer::render(int width, int height) {
    if (!renderer_ && pending_) start();
    lastWidth_ = width;
    lastHeight_ = height;
    if (!renderer_ || failed_) {
        glViewport(0, 0, width, height);
        glClearColor(options_.placeholder[0], options_.placeholder[1], options_.placeholder[2], 1);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }
    if (pointer_[0] >= 0)
        renderer_->setPointer(float(pointer_[0] / width), float(pointer_[1] / height));
    renderer_->setPointerDown(leftDown_);
    renderer_->render(width, height);
}

void NativeRenderer::setPointer(const std::string& /*output*/, double x, double y) {
    pointer_[0] = x;
    pointer_[1] = y;
}

void NativeRenderer::setPointerButton(const std::string& /*output*/, uint32_t button, bool pressed) {
    constexpr uint32_t kButtonLeft = 272;  // BTN_LEFT, linux/input-event-codes.h
    if (button == kButtonLeft) leftDown_ = pressed;
}

void NativeRenderer::setProperty(const std::string& name, const std::string& value) {
    if (renderer_) renderer_->setProperty(name, value);
    else if (pending_) scene::applyProperties(*pending_, {{name, value}});
}

std::vector<std::string> NativeRenderer::problems() const {
    return renderer_ ? renderer_->problems() : std::vector<std::string>{};
}

}  // namespace canvas::scenehost
