// Other clients' toplevel windows on wlroots family compositors, through
// zwlr_foreign_toplevel_manager_v1 (Sway, Hyprland, labwc, river, ...).
// KWin does not offer the protocol; on Plasma the KDE bridge reports windows
// instead (desktop/KdeBridge.h).
//
// start() binds the manager (version 3 at most) and keeps one record per
// toplevel handle: app_id, the wl_outputs it entered, and its state flags.
// Changes are double buffered by the protocol and published on the handle's
// "done" event or when a handle closes: the callback then receives the full
// window list with outputs translated to connector names. Outputs that are
// not (or no longer) known to WaylandConnection are left out.
#pragma once

#include <wayland-client.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "display/Platform.h"

struct zwlr_foreign_toplevel_manager_v1;
struct zwlr_foreign_toplevel_handle_v1;

namespace canvas {

class WaylandConnection;

class WaylandToplevels {
public:
    explicit WaylandToplevels(WaylandConnection& conn) : conn_(conn) {}
    ~WaylandToplevels();
    WaylandToplevels(const WaylandToplevels&) = delete;
    WaylandToplevels& operator=(const WaylandToplevels&) = delete;

    // False when the compositor does not advertise the manager.
    bool start(Platform::ToplevelsCallback cb);

    struct Handle {
        WaylandToplevels* owner = nullptr;
        zwlr_foreign_toplevel_handle_v1* handle = nullptr;
        std::string appId;
        std::vector<wl_output*> outputs;
        bool fullscreen = false;
        bool active = false;
    };

    // Protocol callbacks (static trampolines in the .cpp).
    void addHandle(zwlr_foreign_toplevel_handle_v1* handle);
    void closeHandle(Handle* handle);
    void publish();
    void managerFinished();

private:
    WaylandConnection& conn_;
    zwlr_foreign_toplevel_manager_v1* manager_ = nullptr;
    std::vector<std::unique_ptr<Handle>> handles_;
    Platform::ToplevelsCallback cb_;
};

}  // namespace canvas
