// Platform implementation for Wayland compositors with wlr layer shell.
// Owns the WaylandConnection and the shared EglContext.
#pragma once

#include "display/Platform.h"
#include "display/wayland/WaylandConnection.h"
#include "gl/EglContext.h"

namespace canvas {

class WaylandPlatform : public Platform {
public:
    bool init(EventLoop& loop) override;
    std::vector<OutputDesc> outputs() const override;
    std::unique_ptr<RenderSurface> createSurface(
        const std::string& outputName, const std::string& layer) override;
    void setOnOutputsChanged(std::function<void()> cb) override {
        conn_.setOnOutputsChanged(std::move(cb));
    }
    bool isWayland() const override { return true; }
    void setOnPointer(PointerCallback cb) override {
        conn_.setOnPointer(std::move(cb));
    }

private:
    WaylandConnection conn_;
    EglContext egl_;
};

}  // namespace canvas
