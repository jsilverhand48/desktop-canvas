// desktop-canvas daemon entry point.
//
// Modes:
//   desktop-canvas --list [--json]      scan and print the wallpaper library
//   desktop-canvas [flags]              run the daemon on the current display
//
// Rotation: --playlist FILE --rotate SECONDS cycles random ids from a text
// file. Every output shows the same pick unless --rotate-output narrows the
// set. See render/Rotator.h.
// Flag precedence: defaults < config file < command line. See
// config/Config.h for the file schema and ipc/Protocol.h for the control
// protocol used by desktop-canvasctl.
#include <cstdlib>
#include <iomanip>
#include <iostream>

#include <CLI/CLI11.hpp>
#include <nlohmann/json.hpp>

#include "audio/AudioPolicy.h"
#include "config/Config.h"
#include "core/EventLoop.h"
#include "core/Log.h"
#include "display/Platform.h"
#include "ipc/IpcServer.h"
#include "ipc/Protocol.h"
#include "process/ChildSupervisor.h"
#include "process/LweCommandBuilder.h"
#include "render/InstanceManager.h"
#include "render/Rotator.h"
#include "source/WorkshopScanner.h"

#ifdef CANVAS_HAVE_WAYLAND
#include "display/wayland/WaylandPlatform.h"
#endif
#ifdef CANVAS_HAVE_X11
#include "display/x11/X11Platform.h"
#endif

using nlohmann::json;

namespace canvas {
namespace {

void printList(const std::vector<WallpaperInfo>& items, bool asJson) {
    if (asJson) {
        json j = json::array();
        for (const auto& item : items) {
            j.push_back({{"id", item.id},
                         {"type", toString(item.type)},
                         {"title", item.title},
                         {"file", item.file},
                         {"dir", item.dir.string()},
                         {"reason", item.unsupportedReason}});
        }
        std::cout << j.dump(2) << "\n";
        return;
    }
    int counts[4] = {0, 0, 0, 0};
    for (const auto& item : items) {
        counts[static_cast<int>(item.type)]++;
        std::cout << std::left << std::setw(12) << item.id << std::setw(12)
                  << toString(item.type) << item.title;
        if (!item.unsupportedReason.empty())
            std::cout << "  [" << item.unsupportedReason << "]";
        std::cout << "\n";
    }
    std::cout << "total " << items.size() << ": " << counts[0] << " scene, "
              << counts[1] << " video, " << counts[2] << " web, " << counts[3]
              << " unsupported\n";
}

std::string handleIpc(const std::string& line, EventLoop& loop,
                      InstanceManager& mgr, Rotator& rotator, Config& config) {
    json req = json::parse(line, nullptr, false);
    if (req.is_discarded() || !req.is_object())
        return R"({"ok": false, "error": "malformed request"})";
    std::string cmd = req.value("cmd", "");
    json reply = {{"ok", true}};
    if (cmd == "status") {
        reply["running"] = mgr.running();
        reply["playlist"] = config.playlistFile.string();
        reply["playlist_size"] = rotator.size();
        reply["interval"] = config.playlistInterval;
        reply["rotating"] = rotator.active();
        reply["next_in"] = rotator.secondsRemaining();
        reply["rotate_outputs"] = config.playlistOutputs;
    } else if (cmd == "list") {
        json items = json::array();
        for (const auto& item : mgr.items())
            items.push_back({{"id", item.id},
                             {"type", toString(item.type)},
                             {"title", item.title},
                             {"reason", item.unsupportedReason}});
        reply["items"] = items;
    } else if (cmd == "set") {
        std::string error;
        if (!mgr.setAssignment(req.value("output", "*"), req.value("id", ""),
                               error))
            reply = {{"ok", false}, {"error", error}};
    } else if (cmd == "pause") {
        mgr.pauseAll();
    } else if (cmd == "resume") {
        mgr.resumeAll();
    } else if (cmd == "mute") {
        mgr.setMuted(true);
    } else if (cmd == "unmute") {
        mgr.setMuted(false);
    } else if (cmd == "reload") {
        mgr.rescan();
        mgr.apply();
    } else if (cmd == "playlist") {
        // "" clears the playlist and stops rotation.
        std::string file = req.value("file", "");
        config.playlistFile = Config::expandUser(file);
        config.save(config.sourcePath);
        if (config.playlistFile.empty()) {
            rotator.stop();
        } else if (!rotator.start()) {
            reply = {{"ok", false},
                     {"error", "cannot read playlist " + file}};
        }
    } else if (cmd == "rotate") {
        // Interval in seconds; 0 stops rotation but keeps the playlist.
        int interval = req.value("interval", config.playlistInterval);
        if (interval < 0) {
            reply = {{"ok", false}, {"error", "interval must be >= 0"}};
        } else {
            config.playlistInterval = interval;
            if (req.contains("outputs") && req["outputs"].is_array()) {
                config.playlistOutputs.clear();
                for (const auto& o : req["outputs"])
                    if (o.is_string())
                        config.playlistOutputs.push_back(o.get<std::string>());
            }
            config.save(config.sourcePath);
            if (interval == 0) {
                rotator.stop();
            } else if (config.playlistFile.empty()) {
                reply = {{"ok", false},
                         {"error", "no playlist set (use: playlist <file>)"}};
            } else if (!rotator.start()) {
                reply = {{"ok", false},
                         {"error", "cannot read playlist " +
                                       config.playlistFile.string()}};
            }
        }
    } else if (cmd == "next") {
        if (!rotator.active()) {
            reply = {{"ok", false}, {"error", "rotation is not running"}};
        } else {
            rotator.advance();
            reply["id"] = rotator.current();
        }
    } else if (cmd == "quit") {
        loop.quit(0);
    } else {
        reply = {{"ok", false}, {"error", "unknown command " + cmd}};
    }
    return reply.dump();
}

int runDaemon(Config& config) {
    // Qt child processes (web helper) route their warnings to journald when
    // stderr is not a tty; force them onto stderr so the daemon log has
    // them.
    setenv("QT_FORCE_STDERR_LOGGING", "1", 0);

    EventLoop loop;

    std::unique_ptr<Platform> platform;
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    const char* x11 = std::getenv("DISPLAY");
#ifdef CANVAS_HAVE_WAYLAND
    if (!platform && wayland && *wayland) {
        auto p = std::make_unique<WaylandPlatform>();
        if (p->init(loop)) platform = std::move(p);
    }
#endif
#ifdef CANVAS_HAVE_X11
    if (!platform && x11 && *x11) {
        auto p = std::make_unique<X11Platform>();
        if (p->init(loop)) platform = std::move(p);
    }
#endif
    if (!platform) {
        log::error() << "no usable display platform (WAYLAND_DISPLAY="
                     << (wayland ? wayland : "") << " DISPLAY="
                     << (x11 ? x11 : "") << ")";
        return 1;
    }

    // Verify the scene engine is reachable; scenes are 72 percent of a
    // typical Wallpaper Engine library so a missing binary deserves a loud
    // warning (but not a fatal error; video/web still work).
    std::string help = LweCommandBuilder::probeHelp(config.sceneEnginePath);
    if (help.empty()) {
        log::warn() << "linux-wallpaperengine not found; scene wallpapers "
                       "will not render (set scene.engine_path or install it)";
    } else if (help.find("--screen-root") == std::string::npos ||
               help.find("--silent") == std::string::npos) {
        log::warn() << "installed linux-wallpaperengine has unexpected flags; "
                       "scene control may misbehave";
    }

    AudioPolicy audio(config.audioMuted, config.audioVolume);
    ChildSupervisor supervisor(loop);
    InstanceManager manager(loop, platform.get(), config, audio, supervisor);

    Rotator rotator(loop, manager, config);

    IpcServer ipc(loop, [&](const std::string& line) {
        return handleIpc(line, loop, manager, rotator, config);
    });
    ipc.listen(ipc::socketPath());

    platform->setOnOutputsChanged([&] { manager.apply(); });
    loop.onTerminate([&] {
        log::info() << "shutting down";
        supervisor.stopAll();
        loop.quit(0);
    });

    manager.apply();
    // After the first apply so a rotation pick replaces the config wallpaper
    // rather than racing it; start() is a no op unless a playlist and a
    // positive interval are both configured.
    rotator.start();
    return loop.run();
}

}  // namespace
}  // namespace canvas

int main(int argc, char** argv) {
    CLI::App app{"desktop-canvas: animated wallpapers for Linux desktops"};

    bool list = false, asJson = false, unmute = false;
    std::string configPath;
    std::vector<std::string> roots;
    std::vector<std::string> sets;
    int fps = -1, rotate = -1;
    std::string scaling, layer, engine, assets, playlist;
    std::vector<std::string> rotateOutputs;

    app.add_flag("--list", list, "Scan the wallpaper library and exit");
    app.add_flag("--json", asJson, "With --list, print JSON");
    app.add_option("--config", configPath, "Config file path");
    app.add_option("--root", roots, "Workshop content root (repeatable)");
    app.add_option("--set", sets,
                   "Assignment OUTPUT=ID or just ID for all outputs "
                   "(repeatable, not persisted)");
    app.add_option("--fps", fps, "FPS cap for scene wallpapers");
    app.add_option("--scaling", scaling, "fill | fit | stretch");
    app.add_option("--layer", layer, "background | bottom (Wayland)");
    app.add_flag("--unmute", unmute, "Play wallpaper audio (default muted)");
    app.add_option("--engine", engine, "linux-wallpaperengine binary path");
    app.add_option("--assets", assets, "Wallpaper Engine assets directory");
    app.add_option("--playlist", playlist,
                   "File of wallpaper ids, one per line, to rotate through");
    app.add_option("--rotate", rotate,
                   "Seconds each wallpaper shows before a new random one is "
                   "picked (0 disables rotation)");
    app.add_option("--rotate-output", rotateOutputs,
                   "Limit rotation to this output (repeatable; default is "
                   "every output, all showing the same wallpaper)");
    CLI11_PARSE(app, argc, argv);

    auto path = configPath.empty() ? canvas::Config::defaultPath()
                                   : std::filesystem::path(configPath);
    canvas::Config config = canvas::Config::load(path);
    for (const auto& r : roots) config.contentRoots.emplace_back(r);
    if (fps > 0) config.fps = fps;
    if (!scaling.empty()) config.scaling = scaling;
    if (!layer.empty()) config.layer = layer;
    if (unmute) config.audioMuted = false;
    if (!engine.empty()) config.sceneEnginePath = engine;
    if (!assets.empty()) config.sceneAssetsDir = assets;
    if (!playlist.empty()) config.playlistFile = canvas::Config::expandUser(playlist);
    if (rotate >= 0) config.playlistInterval = rotate;
    if (!rotateOutputs.empty()) config.playlistOutputs = rotateOutputs;
    for (const auto& s : sets) {
        auto eq = s.find('=');
        if (eq == std::string::npos)
            config.assignments["*"] = s;
        else
            config.assignments[s.substr(0, eq)] = s.substr(eq + 1);
    }

    if (list) {
        auto scanRoots = config.contentRoots.empty()
                             ? canvas::WorkshopScanner::defaultRoots()
                             : config.contentRoots;
        canvas::printList(canvas::WorkshopScanner::scan(scanRoots), asJson);
        return 0;
    }
    return canvas::runDaemon(config);
}
