// Platform implementation for X11 sessions. Owns the X11Connection and the
// shared EglContext. Scene children (linux-wallpaperengine --window mode)
// get their windows demoted to the desktop layer via
// X11Connection::demoteWindowsOf, driven by InstanceManager registering the
// supervised pids.
#pragma once

#include <memory>

#include "display/Platform.h"
#include "display/x11/X11Connection.h"
#include "gl/EglContext.h"
#include "input/X11PointerPoller.h"

namespace canvas {

class X11Platform : public Platform {
public:
    bool init(EventLoop& loop) override;
    std::vector<OutputDesc> outputs() const override;
    std::unique_ptr<RenderSurface> createSurface(
        const std::string& outputName, const std::string& layer) override;
    void setOnOutputsChanged(std::function<void()> cb) override {
        conn_.setOnOutputsChanged(std::move(cb));
    }
    bool isWayland() const override { return false; }
    void demoteWindowsOf(std::function<bool(pid_t)> predicate) override {
        conn_.demoteWindowsOf(std::move(predicate));
    }
    void setOnPointer(PointerCallback cb) override {
        if (!poller_)
            poller_ = std::make_unique<X11PointerPoller>(*loop_, conn_);
        poller_->setCallback(std::move(cb));
    }

    X11Connection& connection() { return conn_; }

private:
    EventLoop* loop_ = nullptr;
    X11Connection conn_;
    EglContext egl_;
    std::unique_ptr<X11PointerPoller> poller_;
    int fps_ = 30;
};

}  // namespace canvas
