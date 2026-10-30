// Implementation of WebApi declared in WebApi.h. See the header for routes.
#include "http/WebApi.h"

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include <nlohmann/json.hpp>

#include "control/Controller.h"
#include "render/InstanceManager.h"

namespace canvas {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

HttpResponse ok(const json& body) {
    HttpResponse r = HttpResponse::json(200, body.dump());
    r.headers.push_back({"Cache-Control", "no-store"});
    return r;
}

HttpResponse fail(int status, const std::string& error) {
    return HttpResponse::json(status, json{{"error", error}}.dump());
}

// Workshop ids and UI file names: no separators, no "..".
bool safeName(const std::string& s) {
    if (s.empty() || s == "." || s == "..") return false;
    return std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '.' || c == '_' || c == '-';
    });
}

bool parseBody(const HttpRequest& req, json& out) {
    out = json::parse(req.body.empty() ? "{}" : req.body, nullptr, false);
    return !out.is_discarded() && out.is_object();
}

// "/api/wallpapers/123/show" -> {"wallpapers", "123", "show"}
std::vector<std::string> segments(const std::string& path) {
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos < path.size()) {
        size_t slash = path.find('/', pos);
        std::string part = path.substr(pos, slash == std::string::npos
                                                ? std::string::npos
                                                : slash - pos);
        if (!part.empty()) parts.push_back(part);
        if (slash == std::string::npos) break;
        pos = slash + 1;
    }
    return parts;
}

}  // namespace

WebApi::WebApi(Controller& controller, InstanceManager& manager,
               fs::path uiDir)
    : controller_(controller), manager_(manager), uiDir_(std::move(uiDir)) {}

fs::path WebApi::findUiDir() {
    std::error_code ec;
    auto usable = [&](const fs::path& p) {
        return !p.empty() && fs::is_regular_file(p / "index.html", ec);
    };
    if (const char* env = std::getenv("CANVAS_UI_DIR"); env && *env)
        if (usable(env)) return env;
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = '\0';
        fs::path p = fs::path(exe).parent_path().parent_path() / "share" /
                     "desktop-canvas" / "ui";
        if (usable(p)) return p;
    }
#ifdef CANVAS_UI_INSTALL_DIR
    if (usable(CANVAS_UI_INSTALL_DIR)) return CANVAS_UI_INSTALL_DIR;
#endif
#ifdef CANVAS_UI_SOURCE_DIR
    if (usable(CANVAS_UI_SOURCE_DIR)) return CANVAS_UI_SOURCE_DIR;
#endif
    return {};
}

HttpResponse WebApi::handle(const HttpRequest& req) {
    const std::string& path = req.path;
    if (path.rfind("/api/", 0) == 0) return api(req, path.substr(5));
    if (req.method != "GET" && req.method != "HEAD")
        return fail(405, "method not allowed");
    if (path.rfind("/preview/", 0) == 0) return preview(path.substr(9));
    if (path == "/") return staticFile("index.html");
    return staticFile(path.substr(1));
}

HttpResponse WebApi::api(const HttpRequest& req, const std::string& path) {
    auto parts = segments(path);
    const std::string& m = req.method;
    std::string error;
    json body;
    if (m != "GET" && !parseBody(req, body)) return fail(400, "body must be a JSON object");

    if (parts.size() == 1 && parts[0] == "status" && m == "GET")
        return ok(controller_.status());
    if (parts.size() == 1 && parts[0] == "library" && m == "GET")
        return ok(controller_.library());
    if (parts.size() == 1 && parts[0] == "settings") {
        if (m == "GET") return ok(controller_.settings());
        if (m == "PUT") {
            std::vector<std::string> notes;
            if (!controller_.updateSettings(body, error, notes))
                return fail(400, error);
            return ok({{"ok", true}, {"notes", notes}});
        }
    }
    if (parts.size() == 1 && parts[0] == "wallpapers" && m == "PUT") {
        std::vector<std::string> ids;
        if (!body.contains("ids") || !body["ids"].is_array() ||
            !body.contains("enabled") || !body["enabled"].is_boolean())
            return fail(400, "expected {\"ids\": [...], \"enabled\": bool}");
        for (const auto& id : body["ids"])
            if (id.is_string() && safeName(id.get<std::string>()))
                ids.push_back(id.get<std::string>());
        if (!controller_.setEnabled(ids, body["enabled"].get<bool>(), error))
            return fail(400, error);
        return ok({{"ok", true}, {"count", ids.size()}});
    }
    if (parts.size() >= 2 && parts[0] == "wallpapers") {
        const std::string& id = parts[1];
        if (!safeName(id)) return fail(400, "bad wallpaper id");
        if (parts.size() == 2 && m == "GET") {
            json out;
            if (!controller_.wallpaper(id, out, error)) return fail(404, error);
            return ok(out);
        }
        if (parts.size() == 2 && m == "PUT") {
            if (!controller_.updateWallpaper(id, body, error))
                return fail(400, error);
            return ok({{"ok", true}});
        }
        if (parts.size() == 3 && parts[2] == "show" && m == "POST") {
            std::string output = body.value("output", "*");
            if (!controller_.show(id, output == "*" ? "" : output, error))
                return fail(400, error);
            return ok({{"ok", true}});
        }
    }
    if (parts.size() == 2 && parts[0] == "actions" && m == "POST") {
        if (!controller_.action(parts[1], error)) return fail(400, error);
        return ok({{"ok", true}});
    }
    return fail(404, "no such endpoint");
}

HttpResponse WebApi::preview(const std::string& id) {
    if (!safeName(id)) return fail(400, "bad wallpaper id");
    const WallpaperInfo* info = manager_.find(id);
    if (!info || info->preview.empty()) return fail(404, "no preview");
    std::error_code ec;
    fs::path dir = fs::weakly_canonical(info->dir, ec);
    fs::path file = fs::weakly_canonical(info->dir / info->preview, ec);
    // The preview name comes from project.json; keep it inside the item.
    auto rel = file.lexically_relative(dir);
    if (ec || rel.empty() || *rel.begin() == ".." || !fs::is_regular_file(file, ec))
        return fail(404, "no preview");
    HttpResponse r;
    r.file = file;
    r.contentType = mimeType(file);
    r.headers.push_back({"Cache-Control", "max-age=86400"});
    return r;
}

HttpResponse WebApi::staticFile(const std::string& name) {
    if (uiDir_.empty())
        return HttpResponse::text(503, "settings UI files are not installed\n");
    if (!safeName(name)) return HttpResponse::text(404, "not found\n");
    fs::path file = uiDir_ / name;
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return HttpResponse::text(404, "not found\n");
    HttpResponse r;
    r.file = file;
    r.contentType = mimeType(file);
    r.headers.push_back({"Cache-Control", "no-cache"});
    return r;
}

}  // namespace canvas
