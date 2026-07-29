// Implementation of X11Platform declared in X11Platform.h.
#include "display/x11/X11Platform.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "core/Log.h"
#include "display/x11/X11DesktopOutput.h"

namespace canvas {

bool X11Platform::init(EventLoop& loop) {
    loop_ = &loop;
    if (!conn_.connect(loop)) return false;
    if (!egl_.init(EGL_PLATFORM_X11_KHR, conn_.display())) return false;
    return true;
}

std::vector<OutputDesc> X11Platform::outputs() const {
    std::vector<OutputDesc> result;
    for (const auto& info : conn_.outputs())
        result.push_back(
            OutputDesc{info.name, info.width, info.height, info.x, info.y});
    return result;
}

std::unique_ptr<RenderSurface> X11Platform::createSurface(
    const std::string& outputName, const std::string& /*layer*/) {
    for (const auto& info : conn_.outputs()) {
        if (info.name == outputName)
            return std::make_unique<X11DesktopOutput>(*loop_, conn_, egl_,
                                                      info, fps_);
    }
    log::error() << "createSurface: unknown x11 output " << outputName;
    return nullptr;
}

}  // namespace canvas
