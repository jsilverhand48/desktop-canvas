// HTTP routes of the settings UI (served by HttpServer, actions via
// Controller).
//
//   GET  /api/status                      Controller::status()
//   GET  /api/settings                    Controller::settings()
//   PUT  /api/settings          {patch}   -> {"ok", "notes": [...]}
//   GET  /api/library                     Controller::library()
//   GET  /api/wallpapers/<id>             Controller::wallpaper()
//   PUT  /api/wallpapers/<id>   {"enabled", "properties", "allowed",
//                                "apply"}
//   PUT  /api/wallpapers        {"ids": [...], "enabled": bool}   bulk
//   POST /api/wallpapers/<id>/show  {"output": "DP-1" | "*"}
//   POST /api/actions/<next|pause|resume|mute|unmute|reload|restart>
//   GET  /preview/<id>                    the item's own preview file
//   GET  /, /<file>                       static UI files (data/ui)
//
// Errors answer {"error": "..."} with 400 (bad input) or 404. Only plain
// file names are served from the UI directory and only a wallpaper's own
// declared preview file inside its directory, so no request can read
// arbitrary paths.
#pragma once

#include <filesystem>

#include "http/HttpServer.h"

namespace canvas {

class Controller;
class InstanceManager;

class WebApi {
public:
    WebApi(Controller& controller, InstanceManager& manager,
           std::filesystem::path uiDir);

    HttpResponse handle(const HttpRequest& request);

    // Where the UI files live: $CANVAS_UI_DIR, then
    // <exe>/../share/desktop-canvas/ui, then the configured install prefix,
    // then the source tree (development builds). Empty when none exists.
    static std::filesystem::path findUiDir();

private:
    HttpResponse api(const HttpRequest& request, const std::string& path);
    HttpResponse preview(const std::string& id);
    HttpResponse staticFile(const std::string& name);

    Controller& controller_;
    InstanceManager& manager_;
    std::filesystem::path uiDir_;
};

}  // namespace canvas
