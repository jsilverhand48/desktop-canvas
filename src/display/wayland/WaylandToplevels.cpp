// Implementation of WaylandToplevels declared in WaylandToplevels.h.
//
// Property events update the record at once but are only published on the
// handle's "done", which is when the protocol says they take effect
// together, so the consumer never sees a half applied change.
#include "display/wayland/WaylandToplevels.h"

#include <algorithm>
#include <cstdint>

#include "core/Log.h"
#include "display/wayland/WaylandConnection.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

namespace canvas {

namespace {

using Handle = WaylandToplevels::Handle;

void handleTitle(void*, zwlr_foreign_toplevel_handle_v1*, const char*) {}

void handleAppId(void* data, zwlr_foreign_toplevel_handle_v1*,
                 const char* appId) {
    static_cast<Handle*>(data)->appId = appId ? appId : "";
}

void handleOutputEnter(void* data, zwlr_foreign_toplevel_handle_v1*,
                       wl_output* output) {
    auto& outputs = static_cast<Handle*>(data)->outputs;
    if (std::find(outputs.begin(), outputs.end(), output) == outputs.end())
        outputs.push_back(output);
}

void handleOutputLeave(void* data, zwlr_foreign_toplevel_handle_v1*,
                       wl_output* output) {
    std::erase(static_cast<Handle*>(data)->outputs, output);
}

void handleState(void* data, zwlr_foreign_toplevel_handle_v1*,
                 wl_array* state) {
    auto* h = static_cast<Handle*>(data);
    h->fullscreen = false;
    h->active = false;
    const auto* values = static_cast<const uint32_t*>(state->data);
    for (size_t i = 0; i < state->size / sizeof(uint32_t); i++) {
        if (values[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN)
            h->fullscreen = true;
        else if (values[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED)
            h->active = true;
    }
}

void handleDone(void* data, zwlr_foreign_toplevel_handle_v1*) {
    static_cast<Handle*>(data)->owner->publish();
}

void handleClosed(void* data, zwlr_foreign_toplevel_handle_v1*) {
    auto* h = static_cast<Handle*>(data);
    h->owner->closeHandle(h);
}

void handleParent(void*, zwlr_foreign_toplevel_handle_v1*,
                  zwlr_foreign_toplevel_handle_v1*) {}

const zwlr_foreign_toplevel_handle_v1_listener kHandleListener = {
    handleTitle,  handleAppId,  handleOutputEnter, handleOutputLeave,
    handleState,  handleDone,   handleClosed,      handleParent,
};

void managerToplevel(void* data, zwlr_foreign_toplevel_manager_v1*,
                     zwlr_foreign_toplevel_handle_v1* handle) {
    static_cast<WaylandToplevels*>(data)->addHandle(handle);
}

void managerFinished(void* data, zwlr_foreign_toplevel_manager_v1*) {
    static_cast<WaylandToplevels*>(data)->managerFinished();
}

const zwlr_foreign_toplevel_manager_v1_listener kManagerListener = {
    managerToplevel, managerFinished};

}  // namespace

WaylandToplevels::~WaylandToplevels() {
    for (auto& h : handles_)
        if (h->handle) zwlr_foreign_toplevel_handle_v1_destroy(h->handle);
    if (manager_) {
        zwlr_foreign_toplevel_manager_v1_stop(manager_);
        zwlr_foreign_toplevel_manager_v1_destroy(manager_);
    }
}

bool WaylandToplevels::start(Platform::ToplevelsCallback cb) {
    cb_ = std::move(cb);
    if (manager_) return true;
    if (!conn_.toplevelManagerName()) return false;
    manager_ = static_cast<zwlr_foreign_toplevel_manager_v1*>(wl_registry_bind(
        conn_.registry(), conn_.toplevelManagerName(),
        &zwlr_foreign_toplevel_manager_v1_interface,
        std::min(conn_.toplevelManagerVersion(), 3u)));
    zwlr_foreign_toplevel_manager_v1_add_listener(manager_, &kManagerListener,
                                                  this);
    conn_.flush();
    log::info() << "watching toplevel windows (wlr-foreign-toplevel)";
    return true;
}

void WaylandToplevels::addHandle(zwlr_foreign_toplevel_handle_v1* handle) {
    auto h = std::make_unique<Handle>();
    h->owner = this;
    h->handle = handle;
    zwlr_foreign_toplevel_handle_v1_add_listener(handle, &kHandleListener,
                                                 h.get());
    handles_.push_back(std::move(h));
}

void WaylandToplevels::closeHandle(Handle* handle) {
    zwlr_foreign_toplevel_handle_v1_destroy(handle->handle);
    std::erase_if(handles_, [handle](const auto& h) { return h.get() == handle; });
    publish();
}

void WaylandToplevels::managerFinished() {
    // The compositor stopped sending; keep the last known handles until
    // they close, but the manager object is gone.
    if (manager_) zwlr_foreign_toplevel_manager_v1_destroy(manager_);
    manager_ = nullptr;
}

void WaylandToplevels::publish() {
    if (!cb_) return;
    std::vector<ToplevelInfo> windows;
    windows.reserve(handles_.size());
    for (const auto& h : handles_) {
        ToplevelInfo info;
        info.appId = h->appId;
        info.fullscreen = h->fullscreen;
        info.active = h->active;
        for (wl_output* output : h->outputs)
            for (const auto& known : conn_.outputs())
                if (known->output == output && !known->name.empty())
                    info.outputs.push_back(known->name);
        windows.push_back(std::move(info));
    }
    cb_(windows);
}

}  // namespace canvas
