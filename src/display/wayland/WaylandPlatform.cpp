// Implementation of WaylandPlatform declared in WaylandPlatform.h.
#include "display/wayland/WaylandPlatform.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "core/Log.h"
#include "display/wayland/WaylandLayerOutput.h"
#include "display/wayland/WaylandWindowOutput.h"

namespace canvas {

bool WaylandPlatform::init(EventLoop& loop) {
    if (!conn_.connect(loop)) return false;
    if (!egl_.init(EGL_PLATFORM_WAYLAND_KHR, conn_.display(), eglOptions_))
        return false;
    return true;
}

std::vector<OutputDesc> WaylandPlatform::outputs() const {
    std::vector<OutputDesc> result;
    for (const auto& info : conn_.outputs()) {
        if (info->name.empty()) continue;
        result.push_back(OutputDesc{info->name, info->logical_width,
                                    info->logical_height, info->logical_x,
                                    info->logical_y});
    }
    return result;
}

bool WaylandPlatform::watchToplevels(ToplevelsCallback cb) {
    if (!conn_.toplevelManagerName()) return false;
    if (!toplevels_) toplevels_ = std::make_unique<WaylandToplevels>(conn_);
    return toplevels_->start(std::move(cb));
}

std::unique_ptr<RenderSurface> WaylandPlatform::createSurface(
    const std::string& outputName, const std::string& layer) {
    for (const auto& info : conn_.outputs()) {
        if (info->name == outputName)
            return std::make_unique<WaylandLayerOutput>(conn_, egl_, *info,
                                                        layer);
    }
    log::error() << "createSurface: unknown output " << outputName;
    return nullptr;
}

std::unique_ptr<RenderSurface> WaylandPlatform::createWindow(int width, int height,
                                                             const std::string& title,
                                                             std::function<void()> onClose) {
    if (!conn_.wmBase()) {
        log::error() << "createWindow: the compositor offers no xdg_wm_base";
        return nullptr;
    }
    auto window = std::make_unique<WaylandWindowOutput>(conn_, egl_, width, height, title);
    window->setOnClose(std::move(onClose));
    return window;
}

}  // namespace canvas
