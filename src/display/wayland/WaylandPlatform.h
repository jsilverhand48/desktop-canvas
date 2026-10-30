// Platform implementation for Wayland compositors with wlr layer shell.
// Owns the WaylandConnection and the shared EglContext, created with the
// EglContext::Options given at construction (the daemon uses the defaults;
// the scene helper asks for a GL 3.3 core context).
#pragma once

#include "display/Platform.h"
#include "display/wayland/WaylandConnection.h"
#include "gl/EglContext.h"

namespace canvas {

class WaylandPlatform : public Platform {
public:
    explicit WaylandPlatform(EglContext::Options eglOptions = {})
        : eglOptions_(eglOptions) {}

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

    EglContext& egl() { return egl_; }

private:
    EglContext::Options eglOptions_;
    WaylandConnection conn_;
    EglContext egl_;
};

}  // namespace canvas
