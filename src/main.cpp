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
// Wiring: this file picks the display Platform (Wayland, then X11) and, when
// built with libmpv (CANVAS_HAVE_VIDEO), installs the in-process video
// backend as InstanceManager's VideoFactory.
#include <malloc.h>

#include <cstdlib>
#include <iomanip>
#include <iostream>

#include <CLI/CLI11.hpp>
#include <nlohmann/json.hpp>

#include "audio/AudioPolicy.h"
#include "config/Config.h"
#include "control/Controller.h"
#include "core/EventLoop.h"
#include "core/Log.h"
#include "display/Platform.h"
#include "http/HttpServer.h"
#include "http/WebApi.h"
#include "ipc/IpcServer.h"
#include "ipc/Protocol.h"
#include "process/ChildSupervisor.h"
#include "process/LweCommandBuilder.h"
#include "render/InstanceManager.h"
#include "render/Rotator.h"
#include "source/LibraryWatcher.h"
#include "source/Playlist.h"
#include "source/WorkshopScanner.h"

#ifdef CANVAS_HAVE_WAYLAND
#include "display/wayland/WaylandPlatform.h"
#endif
#ifdef CANVAS_HAVE_X11
#include "display/x11/X11Platform.h"
#endif
#ifdef CANVAS_HAVE_VIDEO
#include "render/VideoBackend.h"
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

int runDaemon(Config& config) {
    // Qt child processes (web helper) route their warnings to journald when
    // stderr is not a tty; force them onto stderr so the daemon log has
    // them.
    setenv("QT_FORCE_STDERR_LOGGING", "1", 0);

    // libmpv spawns ~60 threads per video and glibc would give each its own
    // malloc arena. Freed memory then stays fragmented across dozens of
    // arenas that malloc_trim cannot fully return, so RSS grew ~100 MB per
    // video played and never came back. Two arenas keep it flat (measured:
    // ~90 MB idle after repeated videos instead of ~350 MB and climbing).
    // Must run before any thread starts. MALLOC_ARENA_MAX still overrides.
    if (!std::getenv("MALLOC_ARENA_MAX")) mallopt(M_ARENA_MAX, 2);

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
    bool lweLayer = LweCommandBuilder::supportsLayer(help);
    if (!help.empty() && !lweLayer && platform->isWayland())
        log::warn() << "linux-wallpaperengine has no --layer flag; scenes use "
                       "its default layer and may cover desktop icons";

    AudioPolicy audio(config.audioMuted, config.audioVolume);
    ChildSupervisor supervisor(loop);
    InstanceManager manager(loop, platform.get(), config, audio, supervisor);
    manager.setSceneLayerSupported(lweLayer);
#ifdef CANVAS_HAVE_VIDEO
    // Video plays in process through libmpv. Injecting it here keeps that
    // dependency out of InstanceManager and the libraries tests link.
    manager.setVideoFactory([&loop, &config, &audio](
                                RenderSurface& surface,
                                const WallpaperInfo& info, bool muted) {
        return std::make_shared<VideoInstance>(
            loop, surface, info.id, info.filePath().string(), config.scaling,
            muted, audio.volume());
    });
#endif

    Rotator rotator(loop, manager, config);

    // Library auto refresh: wallpapers added to or removed from the Steam
    // library (or its drive mounted / unmounted) trigger a rescan, which the
    // settings UI notices through status.library_revision. Every rescan
    // (also from settings or `reload`) re-points the watcher, and with
    // autodetected roots it also polls for libraries appearing later.
    LibraryWatcher libraryWatcher(loop, [&] {
        manager.rescan();
        manager.apply();  // drops wallpapers whose folder vanished
    });
    auto followLibrary = [&] {
        libraryWatcher.watch(manager.roots());
        if (config.contentRoots.empty())
            libraryWatcher.setRootsProvider(&WorkshopScanner::defaultRoots);
        else
            libraryWatcher.setRootsProvider(nullptr);
    };
    manager.setOnRescan(followLibrary);
    followLibrary();

    Controller controller(loop, platform.get(), manager, rotator, audio,
                          config);

    IpcServer ipc(loop, [&](const std::string& line) {
        return controller.handleIpc(line);
    });
    ipc.listen(ipc::socketPath());

    // Browser settings UI on 127.0.0.1 (http/HttpServer.h). A taken port is
    // logged and the daemon carries on without the UI.
    WebApi webApi(controller, manager, WebApi::findUiDir());
    HttpServer http(loop, [&](const HttpRequest& request) {
        return webApi.handle(request);
    });
    if (config.uiEnabled && http.listen(config.uiPort))
        controller.setUiUrl("http://127.0.0.1:" +
                            std::to_string(config.uiPort) + "/");

    platform->setOnOutputsChanged([&] { manager.apply(); });
    loop.onTerminate([&] {
        log::info() << "shutting down";
        supervisor.stopAll();
        loop.quit(0);
    });

    manager.apply();
    // After the first apply so a rotation pick replaces the config wallpaper
    // rather than racing it; start() is a no op unless some wallpaper is
    // enabled and the interval is positive.
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
                   "Import a file of wallpaper ids (one per line) into the "
                   "enabled wallpapers rotated through");
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
    // A legacy ids file was folded into "wallpapers": write it back once so
    // the import is not repeated and "playlist.file" disappears.
    if (!config.importedPlaylistFrom.empty() && !list) config.save(path);
    for (const auto& r : roots) config.contentRoots.emplace_back(r);
    if (fps > 0) config.fps = fps;
    if (!scaling.empty()) config.scaling = scaling;
    if (!layer.empty()) config.layer = layer;
    if (unmute) config.audioMuted = false;
    if (!engine.empty()) config.sceneEnginePath = engine;
    if (!assets.empty()) config.sceneAssetsDir = assets;
    if (!playlist.empty()) {
        canvas::Playlist imported;
        if (!imported.load(canvas::Config::expandUser(playlist))) return 1;
        for (const auto& id : imported.ids())
            config.wallpapers[id].enabled = true;
    }
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
