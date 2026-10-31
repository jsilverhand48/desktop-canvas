// Implementation of WaylandWindowOutput declared in WaylandWindowOutput.h.
#include "display/wayland/WaylandWindowOutput.h"

#include "core/Log.h"
#include "display/wayland/WaylandConnection.h"
#include "gl/EglContext.h"
#include "xdg-shell-client-protocol.h"

namespace canvas {

WaylandWindowOutput::WaylandWindowOutput(WaylandConnection& conn, EglContext& egl, int width,
                                         int height, const std::string& title)
    : conn_(conn), egl_(egl), pending_width_(width), pending_height_(height) {
    surface_ = wl_compositor_create_surface(conn.compositor());
    xdg_surface_ = xdg_wm_base_get_xdg_surface(conn.wmBase(), surface_);
    static const xdg_surface_listener kSurfaceListener = {surfaceConfigure};
    xdg_surface_add_listener(xdg_surface_, &kSurfaceListener, this);
    toplevel_ = xdg_surface_get_toplevel(xdg_surface_);
    static const xdg_toplevel_listener kToplevelListener = {
        toplevelConfigure, toplevelClose, toplevelBounds, toplevelCapabilities};
    xdg_toplevel_add_listener(toplevel_, &kToplevelListener, this);
    xdg_toplevel_set_title(toplevel_, title.c_str());
    xdg_toplevel_set_app_id(toplevel_, "desktop-canvas-scene");
    wl_surface_commit(surface_);  // the compositor answers with configure
    conn_.registerSurface(surface_, name_);
    conn_.flush();
}

WaylandWindowOutput::~WaylandWindowOutput() {
    conn_.unregisterSurface(surface_);
    if (frame_callback_) wl_callback_destroy(frame_callback_);
    if (egl_surface_ != EGL_NO_SURFACE) {
        egl_.releaseCurrent();  // see WaylandLayerOutput's destructor
        egl_.destroySurface(egl_surface_);
    }
    if (egl_window_) wl_egl_window_destroy(egl_window_);
    if (toplevel_) xdg_toplevel_destroy(toplevel_);
    if (xdg_surface_) xdg_surface_destroy(xdg_surface_);
    if (surface_) wl_surface_destroy(surface_);
    conn_.flush();
}

void WaylandWindowOutput::toplevelConfigure(void* data, xdg_toplevel*, int32_t width,
                                            int32_t height, wl_array*) {
    auto* self = static_cast<WaylandWindowOutput*>(data);
    // Zero means the client chooses: keep the size we have or asked for.
    if (width > 0 && height > 0) {
        self->pending_width_ = width;
        self->pending_height_ = height;
    }
}

void WaylandWindowOutput::surfaceConfigure(void* data, xdg_surface* surface, uint32_t serial) {
    auto* self = static_cast<WaylandWindowOutput*>(data);
    xdg_surface_ack_configure(surface, serial);
    const int w = self->pending_width_ > 0 ? self->pending_width_ : 1280;
    const int h = self->pending_height_ > 0 ? self->pending_height_ : 720;
    if (!self->egl_window_) {
        self->egl_window_ = wl_egl_window_create(self->surface_, w, h);
        self->egl_surface_ = self->egl_.createWindowSurface(self->egl_window_);
        log::info() << "window configured " << w << "x" << h;
    } else if (w != self->width_ || h != self->height_) {
        wl_egl_window_resize(self->egl_window_, w, h, 0, 0);
    }
    self->width_ = w;
    self->height_ = h;
    self->configured_ = true;
    if (self->on_ready_) self->on_ready_();
}

void WaylandWindowOutput::toplevelClose(void* data, xdg_toplevel*) {
    auto* self = static_cast<WaylandWindowOutput*>(data);
    self->closed_ = true;
    if (self->on_close_) self->on_close_();
}

void WaylandWindowOutput::toplevelBounds(void*, xdg_toplevel*, int32_t, int32_t) {}

void WaylandWindowOutput::toplevelCapabilities(void*, xdg_toplevel*, wl_array*) {}

bool WaylandWindowOutput::makeCurrent() { return ready() && egl_.makeCurrent(egl_surface_); }

void WaylandWindowOutput::swapBuffers() {
    egl_.swapBuffers(egl_surface_);
    conn_.flush();
}

void WaylandWindowOutput::requestFrame(std::function<void()> cb) {
    frame_cb_ = std::move(cb);
    if (frame_callback_) return;  // already armed
    static const wl_callback_listener kFrameListener = {frameDone};
    frame_callback_ = wl_surface_frame(surface_);
    wl_callback_add_listener(frame_callback_, &kFrameListener, this);
    wl_surface_commit(surface_);
    conn_.flush();
}

void WaylandWindowOutput::frameDone(void* data, wl_callback* cb, uint32_t) {
    auto* self = static_cast<WaylandWindowOutput*>(data);
    wl_callback_destroy(cb);
    self->frame_callback_ = nullptr;
    if (self->frame_cb_) {
        auto fn = std::move(self->frame_cb_);
        self->frame_cb_ = nullptr;
        fn();
    }
}

}  // namespace canvas
