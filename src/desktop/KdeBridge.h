// Plasma integration over the session bus (sd-bus), for what KWin does not
// tell Wayland clients: where the cursor is while the Plasma desktop
// surface covers the wallpaper, and which windows are fullscreen.
//
// start() connects to the session bus, exports the bridge object
// (kObjectPath, interface kInterface, methods Cursor(s) and Windows(s)),
// owns kBusName (CANVAS_APP_ID), and asks KWin over org.kde.KWin /Scripting
// to unload any stale copy of the bridge script, load scriptPath under
// kScriptName, and run it (data/kwin/desktop-canvas-bridge.js.in). When KWin
// restarts the script is loaded again. stop() unloads it and leaves the bus.
//
// Calls are accepted only from KWin's own bus connection (the unique name
// owning org.kde.KWin); anything else gets AccessDenied. Payloads are
// strings: Cursor "x y" in global logical pixels, Windows a JSON array of
// {"app", "output", "active"} for fullscreen windows (parseWindows). The bus
// fd and its timeouts run on the EventLoop; all KWin calls are asynchronous
// except the unload in stop(), which waits at most 500 ms.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "display/Platform.h"

struct sd_bus;
struct sd_bus_message;
struct sd_bus_slot;
struct sd_bus_error;
struct sd_bus_vtable;

namespace canvas {

class EventLoop;

class KdeBridge {
public:
    static constexpr const char* kScriptName = "desktop-canvas-bridge";

    struct Handlers {
        std::function<void(double x, double y)> cursor;  // global logical px
        std::function<void(std::vector<ToplevelInfo>)> windows;
    };

    KdeBridge(EventLoop& loop, Handlers handlers);
    ~KdeBridge();
    KdeBridge(const KdeBridge&) = delete;
    KdeBridge& operator=(const KdeBridge&) = delete;

    // False (logged) without a session bus, when another process owns the
    // bus name (a second daemon), or when exporting fails.
    bool start(const std::string& scriptPath);
    void stop();
    bool running() const { return bus_ != nullptr; }

    // The installed bridge script, else the one generated in the build tree;
    // CANVAS_KWIN_SCRIPT overrides. Empty when none exists.
    static std::string findScript();

    // Windows payload to toplevels (every entry fullscreen). Malformed input,
    // or nesting deeper than 4 levels, yields an empty list.
    static std::vector<ToplevelInfo> parseWindows(const std::string& json);
    // Cursor payload "x y"; false when malformed.
    static bool parseCursor(const std::string& text, double& x, double& y);

    static const char* busName();
    static const char* objectPath();
    static const char* interfaceName();

private:
    static const sd_bus_vtable* vtable();
    static int onCursor(sd_bus_message* m, void* data, sd_bus_error* error);
    static int onWindows(sd_bus_message* m, void* data, sd_bus_error* error);
    static int onKWinOwner(sd_bus_message* m, void* data, sd_bus_error* error);
    static int onUnloaded(sd_bus_message* m, void* data, sd_bus_error* error);
    static int onLoaded(sd_bus_message* m, void* data, sd_bus_error* error);
    static int onRun(sd_bus_message* m, void* data, sd_bus_error* error);

    bool fromKWin(sd_bus_message* m);
    void refreshKWinOwner();
    void loadScript();
    void process();
    void rearm();

    EventLoop& loop_;
    Handlers handlers_;
    sd_bus* bus_ = nullptr;
    sd_bus_slot* object_slot_ = nullptr;
    sd_bus_slot* match_slot_ = nullptr;
    int fd_ = -1;
    uint64_t timer_ = 0;
    std::string script_path_;
    std::string kwin_owner_;  // unique bus name of KWin
    int64_t owner_checked_ms_ = 0;
};

}  // namespace canvas
