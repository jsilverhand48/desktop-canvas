// Implementation of WaylandConnection declared in WaylandConnection.h.
#include "display/wayland/WaylandConnection.h"

#include <sys/epoll.h>

#include <cstring>

#include "core/EventLoop.h"
#include "core/Log.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"

namespace canvas {

namespace {

void outputGeometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t,
                    int32_t, const char*, const char*, int32_t) {}
void outputMode(void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) {}
void outputScale(void* data, wl_output*, int32_t factor) {
    static_cast<OutputInfo*>(data)->scale = factor;
}
void outputName(void* data, wl_output*, const char* name) {
    // wl_output v4 carries the connector name too; xdg-output remains for
    // older compositors.
    static_cast<OutputInfo*>(data)->name = name;
}
void outputDescription(void*, wl_output*, const char*) {}
void outputDone(void* data, wl_output*) {
    static_cast<OutputInfo*>(data)->done = true;
}

const wl_output_listener kOutputListener = {
    outputGeometry, outputMode, outputDone, outputScale,
    outputName, outputDescription,
};

void xdgOutputLogicalPosition(void*, zxdg_output_v1*, int32_t, int32_t) {}
void xdgOutputLogicalSize(void* data, zxdg_output_v1*, int32_t w, int32_t h) {
    auto* info = static_cast<OutputInfo*>(data);
    info->logical_width = w;
    info->logical_height = h;
}
void xdgOutputDone(void*, zxdg_output_v1*) {}
void xdgOutputName(void* data, zxdg_output_v1*, const char* name) {
    auto* info = static_cast<OutputInfo*>(data);
    if (info->name.empty()) info->name = name;
}
void xdgOutputDescription(void*, zxdg_output_v1*, const char*) {}

const zxdg_output_v1_listener kXdgOutputListener = {
    xdgOutputLogicalPosition, xdgOutputLogicalSize, xdgOutputDone,
    xdgOutputName, xdgOutputDescription,
};

}  // namespace

// Pointer events: enter remembers which registered surface has focus,
// motion forwards surface local coordinates while focused.
struct PointerListenerAccess {
    static void enter(void* data, wl_pointer*, uint32_t, wl_surface* surface,
                      wl_fixed_t sx, wl_fixed_t sy) {
        auto* self = static_cast<WaylandConnection*>(data);
        self->pointer_focus_ = surface;
        motion(data, nullptr, 0, sx, sy);
    }
    static void leave(void* data, wl_pointer*, uint32_t, wl_surface*) {
        static_cast<WaylandConnection*>(data)->pointer_focus_ = nullptr;
    }
    static void motion(void* data, wl_pointer*, uint32_t, wl_fixed_t sx,
                       wl_fixed_t sy) {
        auto* self = static_cast<WaylandConnection*>(data);
        if (!self->pointer_focus_ || !self->pointer_cb_) return;
        for (const auto& [surf, name] : self->registered_surfaces_) {
            if (surf == self->pointer_focus_) {
                self->pointer_cb_(name, wl_fixed_to_double(sx),
                                  wl_fixed_to_double(sy));
                return;
            }
        }
    }
    static void button(void*, wl_pointer*, uint32_t, uint32_t, uint32_t,
                       uint32_t) {}
    static void axis(void*, wl_pointer*, uint32_t, uint32_t, wl_fixed_t) {}
    static void frame(void*, wl_pointer*) {}
    static void axisSource(void*, wl_pointer*, uint32_t) {}
    static void axisStop(void*, wl_pointer*, uint32_t, uint32_t) {}
    static void axisDiscrete(void*, wl_pointer*, uint32_t, int32_t) {}
    static void axisValue120(void*, wl_pointer*, uint32_t, int32_t) {}
    static void axisRelativeDirection(void*, wl_pointer*, uint32_t,
                                      uint32_t) {}

    static void seatCapabilities(void* data, wl_seat* seat, uint32_t caps);
};

namespace {
const wl_pointer_listener kPointerListener = {
    PointerListenerAccess::enter,        PointerListenerAccess::leave,
    PointerListenerAccess::motion,       PointerListenerAccess::button,
    PointerListenerAccess::axis,         PointerListenerAccess::frame,
    PointerListenerAccess::axisSource,   PointerListenerAccess::axisStop,
    PointerListenerAccess::axisDiscrete, PointerListenerAccess::axisValue120,
    PointerListenerAccess::axisRelativeDirection,
};

void seatName(void*, wl_seat*, const char*) {}
const wl_seat_listener kSeatListener = {PointerListenerAccess::seatCapabilities,
                                        seatName};
}  // namespace

void PointerListenerAccess::seatCapabilities(void* data, wl_seat* seat,
                                             uint32_t caps) {
    auto* self = static_cast<WaylandConnection*>(data);
    bool has_pointer = caps & WL_SEAT_CAPABILITY_POINTER;
    if (has_pointer && !self->pointer_) {
        self->pointer_ = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(self->pointer_, &kPointerListener, self);
    } else if (!has_pointer && self->pointer_) {
        wl_pointer_destroy(self->pointer_);
        self->pointer_ = nullptr;
        self->pointer_focus_ = nullptr;
    }
}

void WaylandConnection::registryGlobal(void* data, wl_registry* registry,
                                       uint32_t name, const char* interface,
                                       uint32_t version) {
    auto* self = static_cast<WaylandConnection*>(data);
    if (strcmp(interface, wl_seat_interface.name) == 0 && !self->seat_) {
        self->seat_ = static_cast<wl_seat*>(wl_registry_bind(
            registry, name, &wl_seat_interface, std::min(version, 5u)));
        wl_seat_add_listener(self->seat_, &kSeatListener, self);
    } else if (strcmp(interface, wl_compositor_interface.name) == 0) {
        self->compositor_ = static_cast<wl_compositor*>(wl_registry_bind(
            registry, name, &wl_compositor_interface, std::min(version, 4u)));
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        self->layer_shell_ = static_cast<zwlr_layer_shell_v1*>(
            wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface,
                             std::min(version, 4u)));
    } else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
        self->xdg_output_manager_ = static_cast<zxdg_output_manager_v1*>(
            wl_registry_bind(registry, name,
                             &zxdg_output_manager_v1_interface,
                             std::min(version, 3u)));
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        auto info = std::make_unique<OutputInfo>();
        info->registry_name = name;
        info->output = static_cast<wl_output*>(wl_registry_bind(
            registry, name, &wl_output_interface, std::min(version, 4u)));
        wl_output_add_listener(info->output, &kOutputListener, info.get());
        self->setupOutput(*info);
        self->outputs_.push_back(std::move(info));
        if (self->loop_ && self->outputs_changed_cb_)
            self->outputs_changed_cb_();
    }
}

void WaylandConnection::registryGlobalRemove(void* data, wl_registry*,
                                             uint32_t name) {
    auto* self = static_cast<WaylandConnection*>(data);
    for (auto it = self->outputs_.begin(); it != self->outputs_.end(); ++it) {
        if ((*it)->registry_name == name) {
            log::info() << "output removed: " << (*it)->name;
            if ((*it)->xdg_output) zxdg_output_v1_destroy((*it)->xdg_output);
            wl_output_destroy((*it)->output);
            self->outputs_.erase(it);
            if (self->outputs_changed_cb_) self->outputs_changed_cb_();
            return;
        }
    }
}

void WaylandConnection::setupOutput(OutputInfo& info) {
    if (xdg_output_manager_ && !info.xdg_output) {
        info.xdg_output =
            zxdg_output_manager_v1_get_xdg_output(xdg_output_manager_,
                                                  info.output);
        zxdg_output_v1_add_listener(info.xdg_output, &kXdgOutputListener,
                                    &info);
    }
}

bool WaylandConnection::connect(EventLoop& loop) {
    loop_ = nullptr;  // callbacks stay quiet during the initial roundtrips
    display_ = wl_display_connect(nullptr);
    if (!display_) {
        log::error() << "wl_display_connect failed";
        return false;
    }
    registry_ = wl_display_get_registry(display_);
    static const wl_registry_listener kRegistryListener = {
        registryGlobal, registryGlobalRemove};
    wl_registry_add_listener(registry_, &kRegistryListener, this);
    wl_display_roundtrip(display_);  // globals
    // xdg-output manager may have arrived after outputs; attach late.
    for (auto& info : outputs_) setupOutput(*info);
    wl_display_roundtrip(display_);  // output names and geometry

    if (!compositor_) {
        log::error() << "compositor global missing";
        return false;
    }
    if (!layer_shell_) {
        log::error() << "compositor lacks zwlr_layer_shell_v1 "
                        "(GNOME Wayland is not supported)";
        return false;
    }

    loop_ = &loop;
    loop.addFd(wl_display_get_fd(display_), EPOLLIN, [this](uint32_t) {
        if (wl_display_prepare_read(display_) == 0)
            wl_display_read_events(display_);
        dispatchPending();
    });
    return true;
}

void WaylandConnection::dispatchPending() {
    if (wl_display_dispatch_pending(display_) < 0)
        log::error() << "wayland dispatch error";
    wl_display_flush(display_);
}

void WaylandConnection::registerSurface(wl_surface* surface,
                                        const std::string& outputName) {
    registered_surfaces_.emplace_back(surface, outputName);
}

void WaylandConnection::unregisterSurface(wl_surface* surface) {
    if (pointer_focus_ == surface) pointer_focus_ = nullptr;
    std::erase_if(registered_surfaces_,
                  [surface](const auto& p) { return p.first == surface; });
}

void WaylandConnection::flush() {
    if (display_) {
        wl_display_dispatch_pending(display_);
        wl_display_flush(display_);
    }
}

WaylandConnection::~WaylandConnection() {
    if (pointer_) wl_pointer_destroy(pointer_);
    if (seat_) wl_seat_destroy(seat_);
    for (auto& info : outputs_) {
        if (info->xdg_output) zxdg_output_v1_destroy(info->xdg_output);
        if (info->output) wl_output_destroy(info->output);
    }
    if (display_) wl_display_disconnect(display_);
}

}  // namespace canvas
