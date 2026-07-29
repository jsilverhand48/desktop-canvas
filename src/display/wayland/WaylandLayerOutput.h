// RenderSurface implementation on a wlr layer shell surface.
//
// Creates one wl_surface per output, wraps it in a zwlr_layer_surface_v1 on
// the background (or bottom) layer, anchored to all edges with exclusive
// zone -1 so panels do not push it around. Once the compositor sends
// configure, a wl_egl_window and EGLSurface are created at logical size
// times the output's integer scale (fractional-scale support is a follow
// up; the vendored protocol XML is already in protocol/).
//
// Rendering behind icons/panels: the background layer is the lowest layer
// shell layer; panels (top layer docks) always stack above. See the README
// caveat matrix for how DE desktop icon windows interact with this.
#pragma once

#include <EGL/egl.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include <functional>
#include <string>

#include "display/RenderSurface.h"

struct zwlr_layer_surface_v1;

namespace canvas {

class EglContext;
class WaylandConnection;
struct OutputInfo;

class WaylandLayerOutput : public RenderSurface {
public:
    // layer: "background" (default) or "bottom".
    WaylandLayerOutput(WaylandConnection& conn, EglContext& egl,
                       const OutputInfo& info, const std::string& layer);
    ~WaylandLayerOutput() override;

    // RenderSurface
    const std::string& name() const override { return name_; }
    bool ready() const override { return configured_ && egl_surface_; }
    int pixelWidth() const override { return width_ * scale_; }
    int pixelHeight() const override { return height_ * scale_; }
    bool makeCurrent() override;
    void swapBuffers() override;
    void requestFrame(std::function<void()> cb) override;
    void setOnReady(std::function<void()> cb) override { on_ready_ = std::move(cb); }

private:
    static void layerConfigure(void* data, zwlr_layer_surface_v1* surface,
                               uint32_t serial, uint32_t width,
                               uint32_t height);
    static void layerClosed(void* data, zwlr_layer_surface_v1* surface);
    static void frameDone(void* data, wl_callback* cb, uint32_t time);
    void resize(int width, int height);

    WaylandConnection& conn_;
    EglContext& egl_;
    std::string name_;
    int scale_ = 1;
    int width_ = 0;   // logical
    int height_ = 0;  // logical
    bool configured_ = false;

    wl_surface* surface_ = nullptr;
    zwlr_layer_surface_v1* layer_surface_ = nullptr;
    wl_egl_window* egl_window_ = nullptr;
    EGLSurface egl_surface_ = EGL_NO_SURFACE;
    wl_callback* frame_callback_ = nullptr;
    std::function<void()> frame_cb_;
    std::function<void()> on_ready_;
};

}  // namespace canvas
