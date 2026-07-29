// Implementation of WaylandLayerOutput declared in WaylandLayerOutput.h.
#include "display/wayland/WaylandLayerOutput.h"

#include "core/Log.h"
#include "display/wayland/WaylandConnection.h"
#include "gl/EglContext.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

namespace canvas {

WaylandLayerOutput::WaylandLayerOutput(WaylandConnection& conn,
                                       EglContext& egl, const OutputInfo& info,
                                       const std::string& layer)
    : conn_(conn), egl_(egl), name_(info.name), scale_(info.scale) {
    surface_ = wl_compositor_create_surface(conn.compositor());
    uint32_t layer_id = layer == "bottom"
                            ? ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM
                            : ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
    layer_surface_ = zwlr_layer_shell_v1_get_layer_surface(
        conn.layerShell(), surface_, info.output, layer_id, "desktop-canvas");

    static const zwlr_layer_surface_v1_listener kLayerListener = {
        layerConfigure, layerClosed};
    zwlr_layer_surface_v1_add_listener(layer_surface_, &kLayerListener, this);

    zwlr_layer_surface_v1_set_anchor(
        layer_surface_, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                            ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                            ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                            ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface_, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        layer_surface_, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    zwlr_layer_surface_v1_set_size(layer_surface_, 0, 0);  // compositor sizes
    wl_surface_set_buffer_scale(surface_, scale_);
    wl_surface_commit(surface_);
    conn_.registerSurface(surface_, name_);
    conn_.flush();
}

WaylandLayerOutput::~WaylandLayerOutput() {
    conn_.unregisterSurface(surface_);
    if (frame_callback_) wl_callback_destroy(frame_callback_);
    if (egl_surface_ != EGL_NO_SURFACE) egl_.destroySurface(egl_surface_);
    if (egl_window_) wl_egl_window_destroy(egl_window_);
    if (layer_surface_) zwlr_layer_surface_v1_destroy(layer_surface_);
    if (surface_) wl_surface_destroy(surface_);
    conn_.flush();
}

void WaylandLayerOutput::layerConfigure(void* data,
                                        zwlr_layer_surface_v1* surface,
                                        uint32_t serial, uint32_t width,
                                        uint32_t height) {
    auto* self = static_cast<WaylandLayerOutput*>(data);
    zwlr_layer_surface_v1_ack_configure(surface, serial);
    log::info() << "layer surface " << self->name_ << " configured "
                << width << "x" << height << " scale " << self->scale_;
    self->resize(static_cast<int>(width), static_cast<int>(height));
}

void WaylandLayerOutput::layerClosed(void* data, zwlr_layer_surface_v1*) {
    auto* self = static_cast<WaylandLayerOutput*>(data);
    log::warn() << "layer surface closed for " << self->name_;
    self->configured_ = false;
}

void WaylandLayerOutput::resize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    width_ = width;
    height_ = height;
    if (!egl_window_) {
        egl_window_ =
            wl_egl_window_create(surface_, pixelWidth(), pixelHeight());
        egl_surface_ = egl_.createWindowSurface(egl_window_);
    } else {
        wl_egl_window_resize(egl_window_, pixelWidth(), pixelHeight(), 0, 0);
    }
    configured_ = true;
    if (on_ready_) on_ready_();
}

bool WaylandLayerOutput::makeCurrent() {
    return ready() && egl_.makeCurrent(egl_surface_);
}

void WaylandLayerOutput::swapBuffers() {
    egl_.swapBuffers(egl_surface_);
    conn_.flush();
}

void WaylandLayerOutput::requestFrame(std::function<void()> cb) {
    frame_cb_ = std::move(cb);
    if (frame_callback_) return;  // already armed
    static const wl_callback_listener kFrameListener = {frameDone};
    frame_callback_ = wl_surface_frame(surface_);
    wl_callback_add_listener(frame_callback_, &kFrameListener, this);
    wl_surface_commit(surface_);
    conn_.flush();
}

void WaylandLayerOutput::frameDone(void* data, wl_callback* cb, uint32_t) {
    auto* self = static_cast<WaylandLayerOutput*>(data);
    wl_callback_destroy(cb);
    self->frame_callback_ = nullptr;
    if (self->frame_cb_) {
        auto fn = std::move(self->frame_cb_);
        self->frame_cb_ = nullptr;
        fn();
    }
}

}  // namespace canvas
