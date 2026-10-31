// RenderSurface implementation on an ordinary xdg-shell window, for
// desktop-canvas-scene --window (previews and interactive debugging).
//
// Creates a wl_surface with an xdg_surface and xdg_toplevel titled and
// sized as asked; the compositor may resize it (a configure with a non
// zero size), and the EGL window follows. The window's surface is
// registered under the name "window", so pointer motion and buttons over it
// arrive like those over wallpaper surfaces. When the user closes the
// window, closed() turns true and the close callback runs; the owner
// decides what that means (the helper quits). Rendering, pacing and
// readiness follow the RenderSurface contract, like WaylandLayerOutput.
#pragma once

#include <EGL/egl.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include <functional>
#include <string>

#include "display/RenderSurface.h"

struct xdg_surface;
struct xdg_toplevel;

namespace canvas {

class EglContext;
class WaylandConnection;

class WaylandWindowOutput : public RenderSurface {
public:
    WaylandWindowOutput(WaylandConnection& conn, EglContext& egl, int width, int height,
                        const std::string& title);
    ~WaylandWindowOutput() override;

    const std::string& name() const override { return name_; }
    bool ready() const override { return configured_ && egl_surface_; }
    int pixelWidth() const override { return width_; }
    int pixelHeight() const override { return height_; }
    bool makeCurrent() override;
    void swapBuffers() override;
    void requestFrame(std::function<void()> cb) override;
    void setOnReady(std::function<void()> cb) override { on_ready_ = std::move(cb); }
    void show() override {}
    void hide() override {}
    bool closed() const override { return closed_; }

    void setOnClose(std::function<void()> cb) { on_close_ = std::move(cb); }

private:
    static void surfaceConfigure(void* data, xdg_surface* surface, uint32_t serial);
    static void toplevelConfigure(void* data, xdg_toplevel* toplevel, int32_t width,
                                  int32_t height, wl_array* states);
    static void toplevelClose(void* data, xdg_toplevel* toplevel);
    static void toplevelBounds(void* data, xdg_toplevel* toplevel, int32_t width, int32_t height);
    static void toplevelCapabilities(void* data, xdg_toplevel* toplevel, wl_array* caps);
    static void frameDone(void* data, wl_callback* cb, uint32_t time);

    WaylandConnection& conn_;
    EglContext& egl_;
    std::string name_ = "window";
    int width_ = 0, height_ = 0;              // current size
    int pending_width_ = 0, pending_height_ = 0;  // from the last toplevel configure
    bool configured_ = false;
    bool closed_ = false;
    wl_surface* surface_ = nullptr;
    xdg_surface* xdg_surface_ = nullptr;
    xdg_toplevel* toplevel_ = nullptr;
    wl_egl_window* egl_window_ = nullptr;
    EGLSurface egl_surface_ = EGL_NO_SURFACE;
    wl_callback* frame_callback_ = nullptr;
    std::function<void()> frame_cb_;
    std::function<void()> on_ready_;
    std::function<void()> on_close_;
};

}  // namespace canvas
