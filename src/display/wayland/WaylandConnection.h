// Wayland display connection and registry management.
//
// Binds the globals the daemon needs: wl_compositor, zwlr_layer_shell_v1
// (background surfaces), zxdg_output_manager_v1 (output names and logical
// geometry: size and position in the global layout), wl_seat (pointer
// motion and buttons over our own surfaces) and xdg_wm_base (ordinary
// windows for desktop-canvas-scene --window; pings are answered). A
// zwlr_foreign_toplevel_manager_v1 global is only remembered; WaylandToplevels
// binds it when the fullscreen pause asks for other clients' windows. Tracks wl_outputs as they
// appear and disappear; each is described by OutputInfo. The display fd is
// registered with the EventLoop using the prepare_read pattern so the daemon
// stays single threaded.
//
// The compositor must implement zwlr_layer_shell_v1 (KWin, Hyprland, Sway,
// wlroots). GNOME's Mutter does not; connect() fails there and the caller
// reports the unsupported environment.
#pragma once

#include <wayland-client.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

struct zwlr_layer_shell_v1;
struct xdg_wm_base;
struct zxdg_output_manager_v1;
struct zxdg_output_v1;

namespace canvas {

class EventLoop;

struct OutputInfo {
    wl_output* output = nullptr;
    zxdg_output_v1* xdg_output = nullptr;
    uint32_t registry_name = 0;   // for removal matching
    std::string name;             // connector name from xdg-output
    int32_t logical_x = 0;        // position in the global layout (xdg-output)
    int32_t logical_y = 0;
    int32_t logical_width = 0;    // from xdg-output
    int32_t logical_height = 0;
    int32_t scale = 1;            // integer scale from wl_output
    bool done = false;            // received wl_output.done at least once
};

class WaylandConnection {
public:
    WaylandConnection() = default;
    ~WaylandConnection();

    // Connects, binds globals, and does initial roundtrips so outputs and
    // their names are known on return. Returns false when the display or the
    // layer shell is unavailable.
    bool connect(EventLoop& loop);

    wl_display* display() const { return display_; }
    wl_compositor* compositor() const { return compositor_; }
    zwlr_layer_shell_v1* layerShell() const { return layer_shell_; }
    xdg_wm_base* wmBase() const { return wm_base_; }  // null when absent

    const std::vector<std::unique_ptr<OutputInfo>>& outputs() const {
        return outputs_;
    }

    // Fired after an output appears (with name resolved) or disappears.
    void setOnOutputsChanged(std::function<void()> cb) {
        outputs_changed_cb_ = std::move(cb);
    }

    // Pointer support: layer outputs register their wl_surface with the
    // output name; wl_pointer enter/motion events over registered surfaces
    // are reported through the pointer callback in surface local (logical)
    // coordinates. Events only arrive while no other surface is stacked
    // above ours under the cursor (see README caveat matrix).
    void registerSurface(wl_surface* surface, const std::string& outputName);
    void unregisterSurface(wl_surface* surface);
    void setOnPointer(
        std::function<void(const std::string&, double, double)> cb) {
        pointer_cb_ = std::move(cb);
    }
    // Button presses and releases over registered surfaces (Linux input
    // codes, BTN_LEFT = 272).
    void setOnPointerButton(
        std::function<void(const std::string&, uint32_t, bool)> cb) {
        pointer_button_cb_ = std::move(cb);
    }

    // zwlr_foreign_toplevel_manager_v1 as advertised by the compositor
    // (wlroots family; KWin does not offer it): registry name and version,
    // 0 when absent. Bound on demand by WaylandToplevels.
    uint32_t toplevelManagerName() const { return toplevel_manager_name_; }
    uint32_t toplevelManagerVersion() const { return toplevel_manager_version_; }
    wl_registry* registry() const { return registry_; }

    void flush();

private:
    static void registryGlobal(void* data, wl_registry* registry,
                               uint32_t name, const char* interface,
                               uint32_t version);
    static void registryGlobalRemove(void* data, wl_registry* registry,
                                     uint32_t name);
    void setupOutput(OutputInfo& info);
    void setupPointer();
    void dispatchPending();

    wl_display* display_ = nullptr;
    wl_registry* registry_ = nullptr;
    wl_compositor* compositor_ = nullptr;
    zwlr_layer_shell_v1* layer_shell_ = nullptr;
    xdg_wm_base* wm_base_ = nullptr;
    zxdg_output_manager_v1* xdg_output_manager_ = nullptr;
    std::vector<std::unique_ptr<OutputInfo>> outputs_;
    std::function<void()> outputs_changed_cb_;
    EventLoop* loop_ = nullptr;

    wl_seat* seat_ = nullptr;
    wl_pointer* pointer_ = nullptr;
    std::vector<std::pair<wl_surface*, std::string>> registered_surfaces_;
    wl_surface* pointer_focus_ = nullptr;
    std::function<void(const std::string&, double, double)> pointer_cb_;
    std::function<void(const std::string&, uint32_t, bool)> pointer_button_cb_;
    uint32_t toplevel_manager_name_ = 0;
    uint32_t toplevel_manager_version_ = 0;

    friend struct PointerListenerAccess;
};

}  // namespace canvas
