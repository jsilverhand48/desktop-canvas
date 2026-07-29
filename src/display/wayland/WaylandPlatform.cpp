// Implementation of WaylandPlatform declared in WaylandPlatform.h.
#include "display/wayland/WaylandPlatform.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "core/Log.h"
#include "display/wayland/WaylandLayerOutput.h"

namespace canvas {

bool WaylandPlatform::init(EventLoop& loop) {
    if (!conn_.connect(loop)) return false;
    if (!egl_.init(EGL_PLATFORM_WAYLAND_KHR, conn_.display())) return false;
    return true;
}

std::vector<OutputDesc> WaylandPlatform::outputs() const {
    std::vector<OutputDesc> result;
    for (const auto& info : conn_.outputs()) {
        if (info->name.empty()) continue;
        result.push_back(OutputDesc{info->name, info->logical_width,
                                    info->logical_height, 0, 0});
    }
    return result;
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

}  // namespace canvas
