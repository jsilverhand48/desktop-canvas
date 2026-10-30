// Implementation of KdeBridge declared in KdeBridge.h.
//
// sd-bus runs on the EventLoop: its fd is watched with the events
// sd_bus_get_events() asks for, its absolute CLOCK_MONOTONIC timeout becomes
// an addTimerAt deadline, and process() drains sd_bus_process() and re-arms
// both after every wakeup.
#include "desktop/KdeBridge.h"

#include <poll.h>
#include <sys/epoll.h>
#include <systemd/sd-bus.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include <nlohmann/json.hpp>

#include "core/EventLoop.h"
#include "core/Log.h"
#include "process/HelperProtocol.h"

#ifndef CANVAS_APP_ID
#define CANVAS_APP_ID "io.github.jsilverhand48.DesktopCanvas"
#endif

namespace canvas {

namespace fs = std::filesystem;

namespace {

const std::string& objectPathString() {
    static const std::string path = [] {
        std::string p = std::string("/") + CANVAS_APP_ID;
        for (char& c : p)
            if (c == '.') c = '/';
        return p;
    }();
    return path;
}

const std::string& interfaceString() {
    static const std::string name = std::string(CANVAS_APP_ID) + ".Bridge";
    return name;
}

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

const char* KdeBridge::busName() { return CANVAS_APP_ID; }
const char* KdeBridge::objectPath() { return objectPathString().c_str(); }
const char* KdeBridge::interfaceName() { return interfaceString().c_str(); }

KdeBridge::KdeBridge(EventLoop& loop, Handlers handlers)
    : loop_(loop), handlers_(std::move(handlers)) {}

KdeBridge::~KdeBridge() { stop(); }

std::string KdeBridge::findScript() {
    if (const char* forced = std::getenv("CANVAS_KWIN_SCRIPT"); forced && *forced)
        return fs::exists(forced) ? std::string(forced) : std::string();
    std::error_code ec;
    // A build tree keeps the generated script next to the daemon binary.
    auto self = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        auto candidate = self.parent_path() / "kwin" / "desktop-canvas-bridge.js";
        if (fs::exists(candidate, ec)) return candidate.string();
    }
#ifdef CANVAS_KWIN_INSTALL_DIR
    fs::path installed = fs::path(CANVAS_KWIN_INSTALL_DIR) / "desktop-canvas-bridge.js";
    if (fs::exists(installed, ec)) return installed.string();
#endif
    return {};
}

std::vector<ToplevelInfo> KdeBridge::parseWindows(const std::string& json) {
    std::vector<ToplevelInfo> windows;
    if (json.size() > 65536 || helper::jsonDepth(json) > 4) return windows;
    auto list = nlohmann::json::parse(json, nullptr, false);
    if (!list.is_array()) return windows;
    for (const auto& entry : list) {
        if (!entry.is_object() || windows.size() >= 256) continue;
        ToplevelInfo info;
        info.fullscreen = true;
        if (auto app = entry.find("app"); app != entry.end() && app->is_string())
            info.appId = app->get<std::string>().substr(0, 256);
        if (auto out = entry.find("output"); out != entry.end() && out->is_string() &&
                                             !out->get<std::string>().empty())
            info.outputs.push_back(out->get<std::string>().substr(0, 64));
        if (auto active = entry.find("active");
            active != entry.end() && active->is_boolean())
            info.active = active->get<bool>();
        windows.push_back(std::move(info));
    }
    return windows;
}

bool KdeBridge::parseCursor(const std::string& text, double& x, double& y) {
    if (text.size() > 64) return false;
    char tail = 0;
    if (std::sscanf(text.c_str(), "%lf %lf %c", &x, &y, &tail) != 2) return false;
    return std::isfinite(x) && std::isfinite(y);
}

const sd_bus_vtable* KdeBridge::vtable() {
    static const sd_bus_vtable table[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_METHOD("Cursor", "s", "", &KdeBridge::onCursor,
                      SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("Windows", "s", "", &KdeBridge::onWindows,
                      SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_VTABLE_END,
    };
    return table;
}

bool KdeBridge::start(const std::string& scriptPath) {
    if (bus_) return true;
    script_path_ = scriptPath;
    int r = sd_bus_open_user(&bus_);
    if (r < 0) {
        log::warn() << "KDE bridge: no session bus (" << strerror(-r) << ")";
        bus_ = nullptr;
        return false;
    }
    r = sd_bus_add_object_vtable(bus_, &object_slot_, objectPath(),
                                 interfaceName(), vtable(), this);
    if (r >= 0) r = sd_bus_request_name(bus_, busName(), 0);
    if (r < 0) {
        log::warn() << "KDE bridge: cannot own " << busName() << " ("
                    << strerror(-r) << "); is another desktop-canvas running?";
        stop();
        return false;
    }
    // Reload the script when KWin restarts (it takes its scripts with it).
    sd_bus_match_signal(bus_, &match_slot_, "org.freedesktop.DBus",
                        "/org/freedesktop/DBus", "org.freedesktop.DBus",
                        "NameOwnerChanged", &KdeBridge::onKWinOwner, this);
    fd_ = sd_bus_get_fd(bus_);
    loop_.addFd(fd_, EPOLLIN, [this](uint32_t) { process(); });
    refreshKWinOwner();
    if (kwin_owner_.empty())
        log::warn() << "KDE bridge: KWin is not on the session bus yet";
    loadScript();
    process();
    return bus_ != nullptr;
}

void KdeBridge::stop() {
    if (!bus_) return;
    if (timer_) loop_.cancelTimer(timer_);
    timer_ = 0;
    if (fd_ >= 0) loop_.removeFd(fd_);
    fd_ = -1;
    // Best effort: do not leave the script running in KWin.
    sd_bus_set_method_call_timeout(bus_, 500000);
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_call_method(bus_, "org.kde.KWin", "/Scripting",
                       "org.kde.kwin.Scripting", "unloadScript", &error,
                       nullptr, "s", kScriptName);
    sd_bus_error_free(&error);
    match_slot_ = sd_bus_slot_unref(match_slot_);
    object_slot_ = sd_bus_slot_unref(object_slot_);
    sd_bus_release_name(bus_, busName());
    bus_ = sd_bus_flush_close_unref(bus_);
}

void KdeBridge::refreshKWinOwner() {
    owner_checked_ms_ = nowMs();
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    int r = sd_bus_call_method(bus_, "org.freedesktop.DBus",
                               "/org/freedesktop/DBus", "org.freedesktop.DBus",
                               "GetNameOwner", &error, &reply, "s",
                               "org.kde.KWin");
    const char* owner = nullptr;
    if (r >= 0 && sd_bus_message_read(reply, "s", &owner) >= 0 && owner)
        kwin_owner_ = owner;
    else
        kwin_owner_.clear();
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
}

bool KdeBridge::fromKWin(sd_bus_message* m) {
    const char* sender = sd_bus_message_get_sender(m);
    if (!sender) return false;
    if (!kwin_owner_.empty() && kwin_owner_ == sender) return true;
    // KWin may have restarted; look again, at most once a second.
    if (nowMs() - owner_checked_ms_ >= 1000) refreshKWinOwner();
    return !kwin_owner_.empty() && kwin_owner_ == sender;
}

int KdeBridge::onCursor(sd_bus_message* m, void* data, sd_bus_error* error) {
    auto* self = static_cast<KdeBridge*>(data);
    if (!self->fromKWin(m))
        return sd_bus_error_set(error, SD_BUS_ERROR_ACCESS_DENIED,
                                "only KWin may report the cursor");
    const char* text = nullptr;
    int r = sd_bus_message_read(m, "s", &text);
    if (r < 0) return r;
    double x = 0, y = 0;
    if (text && parseCursor(text, x, y) && self->handlers_.cursor)
        self->handlers_.cursor(x, y);
    return sd_bus_reply_method_return(m, "");
}

int KdeBridge::onWindows(sd_bus_message* m, void* data, sd_bus_error* error) {
    auto* self = static_cast<KdeBridge*>(data);
    if (!self->fromKWin(m))
        return sd_bus_error_set(error, SD_BUS_ERROR_ACCESS_DENIED,
                                "only KWin may report windows");
    const char* text = nullptr;
    int r = sd_bus_message_read(m, "s", &text);
    if (r < 0) return r;
    if (text && self->handlers_.windows) self->handlers_.windows(parseWindows(text));
    return sd_bus_reply_method_return(m, "");
}

int KdeBridge::onKWinOwner(sd_bus_message* m, void* data, sd_bus_error*) {
    auto* self = static_cast<KdeBridge*>(data);
    const char *name = nullptr, *oldOwner = nullptr, *newOwner = nullptr;
    if (sd_bus_message_read(m, "sss", &name, &oldOwner, &newOwner) < 0 || !name ||
        std::strcmp(name, "org.kde.KWin") != 0)
        return 0;
    self->kwin_owner_ = newOwner ? newOwner : "";
    if (!self->kwin_owner_.empty()) {
        log::info() << "KDE bridge: KWin (re)appeared, loading the script";
        self->loadScript();
    }
    return 0;
}

void KdeBridge::loadScript() {
    if (!bus_ || script_path_.empty()) return;
    // Unload a copy a crashed daemon may have left, then load and run.
    sd_bus_call_method_async(bus_, nullptr, "org.kde.KWin", "/Scripting",
                             "org.kde.kwin.Scripting", "unloadScript",
                             &KdeBridge::onUnloaded, this, "s", kScriptName);
}

int KdeBridge::onUnloaded(sd_bus_message*, void* data, sd_bus_error*) {
    auto* self = static_cast<KdeBridge*>(data);
    sd_bus_call_method_async(self->bus_, nullptr, "org.kde.KWin", "/Scripting",
                             "org.kde.kwin.Scripting", "loadScript",
                             &KdeBridge::onLoaded, self, "ss",
                             self->script_path_.c_str(), kScriptName);
    return 0;
}

int KdeBridge::onLoaded(sd_bus_message* m, void* data, sd_bus_error*) {
    auto* self = static_cast<KdeBridge*>(data);
    if (sd_bus_message_is_method_error(m, nullptr)) {
        const sd_bus_error* e = sd_bus_message_get_error(m);
        log::warn() << "KDE bridge: KWin did not load the script: "
                    << (e && e->message ? e->message : "unknown error");
        return 0;
    }
    int32_t id = -1;
    if (sd_bus_message_read(m, "i", &id) < 0 || id < 0) {
        log::warn() << "KDE bridge: KWin refused " << self->script_path_;
        return 0;
    }
    std::string path = "/Scripting/Script" + std::to_string(id);
    sd_bus_call_method_async(self->bus_, nullptr, "org.kde.KWin", path.c_str(),
                             "org.kde.kwin.Script", "run", &KdeBridge::onRun,
                             self, "");
    return 0;
}

int KdeBridge::onRun(sd_bus_message* m, void*, sd_bus_error*) {
    if (sd_bus_message_is_method_error(m, nullptr)) {
        const sd_bus_error* e = sd_bus_message_get_error(m);
        log::warn() << "KDE bridge: KWin could not run the script: "
                    << (e && e->message ? e->message : "unknown error");
    } else {
        log::info() << "KDE bridge: script running in KWin (cursor and "
                       "fullscreen windows)";
    }
    return 0;
}

void KdeBridge::process() {
    if (!bus_) return;
    for (;;) {
        int r = sd_bus_process(bus_, nullptr);
        if (r < 0) {
            log::warn() << "KDE bridge: bus connection lost (" << strerror(-r)
                        << ")";
            stop();
            return;
        }
        if (r == 0) break;
    }
    rearm();
}

void KdeBridge::rearm() {
    if (!bus_ || fd_ < 0) return;
    int events = sd_bus_get_events(bus_);
    uint32_t mask = EPOLLIN;
    if (events > 0 && (events & POLLOUT)) mask |= EPOLLOUT;
    loop_.modifyFd(fd_, mask);
    if (timer_) loop_.cancelTimer(timer_);
    timer_ = 0;
    uint64_t usec = 0;
    if (sd_bus_get_timeout(bus_, &usec) >= 0 && usec != UINT64_MAX) {
        auto deadline = std::chrono::steady_clock::time_point(
            std::chrono::microseconds(usec));
        timer_ = loop_.addTimerAt(deadline, [this] {
            timer_ = 0;
            process();
        });
    }
}

}  // namespace canvas
