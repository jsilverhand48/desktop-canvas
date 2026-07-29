// Wayland display connection and registry management.
//
// Binds the globals the daemon needs: wl_compositor, zwlr_layer_shell_v1
// (background surfaces), zxdg_output_manager_v1 (output names and logical
// geometry), and wl_seat (pointer input, M5). Tracks wl_outputs as they
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
struct zxdg_output_manager_v1;
struct zxdg_output_v1;

namespace canvas {

class EventLoop;

struct OutputInfo {
    wl_output* output = nullptr;
    zxdg_output_v1* xdg_output = nullptr;
    uint32_t registry_name = 0;   // for removal matching
    std::string name;             // connector name from xdg-output
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
    zxdg_output_manager_v1* xdg_output_manager_ = nullptr;
    std::vector<std::unique_ptr<OutputInfo>> outputs_;
    std::function<void()> outputs_changed_cb_;
    EventLoop* loop_ = nullptr;

    wl_seat* seat_ = nullptr;
    wl_pointer* pointer_ = nullptr;
    std::vector<std::pair<wl_surface*, std::string>> registered_surfaces_;
    wl_surface* pointer_focus_ = nullptr;
    std::function<void(const std::string&, double, double)> pointer_cb_;

    friend struct PointerListenerAccess;
};

}  // namespace canvas
