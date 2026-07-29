// Implementation of WorkshopScanner declared in WorkshopScanner.h.
//
// Type matching is case insensitive because real libraries contain both
// "scene" and "Scene" etc. Items with a top level "dependency" key are
// Wallpaper Engine presets layered on another wallpaper; they are marked
// unsupported rather than skipped so UIs can show why.
#include "source/WorkshopScanner.h"

#include <algorithm>
#include <cctype>
#include <fstream>

#include <nlohmann/json.hpp>

#include "core/Log.h"

namespace canvas {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

WallpaperInfo unsupported(const fs::path& dir, std::string reason,
                          std::string title = "") {
    WallpaperInfo info;
    info.id = dir.filename().string();
    info.dir = dir;
    info.type = WallpaperType::Unsupported;
    info.unsupportedReason = std::move(reason);
    info.title = std::move(title);
    return info;
}

}  // namespace

std::optional<WallpaperInfo> WorkshopScanner::scanItem(const fs::path& itemDir) {
    fs::path projectFile = itemDir / "project.json";
    std::error_code ec;
    if (!fs::exists(projectFile, ec)) return std::nullopt;

    std::ifstream in(projectFile);
    if (!in) return unsupported(itemDir, "unreadable project.json");

    json project = json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (project.is_discarded() || !project.is_object())
        return unsupported(itemDir, "malformed project.json");

    WallpaperInfo info;
    info.id = itemDir.filename().string();
    info.dir = itemDir;
    info.title = project.value("title", "");
    info.file = project.value("file", "");
    info.preview = project.value("preview", "");

    if (project.contains("dependency"))
        return unsupported(itemDir, "preset (depends on another wallpaper)",
                           info.title);

    std::string type = lower(project.value("type", ""));
    if (type == "scene")
        info.type = WallpaperType::Scene;
    else if (type == "video")
        info.type = WallpaperType::Video;
    else if (type == "web")
        info.type = WallpaperType::Web;
    else
        return unsupported(itemDir, "unknown type \"" + type + "\"", info.title);

    if (info.file.empty())
        return unsupported(itemDir, "project.json has no file entry",
                           info.title);
    if (info.type != WallpaperType::Scene &&
        !fs::exists(info.filePath(), ec))
        return unsupported(itemDir, "payload file missing: " + info.file,
                           info.title);
    return info;
}

std::vector<WallpaperInfo> WorkshopScanner::scan(
    const std::vector<fs::path>& roots) {
    std::vector<WallpaperInfo> items;
    for (const auto& root : roots) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) {
            log::debug() << "content root missing, skipping: " << root;
            continue;
        }
        for (const auto& entry : fs::directory_iterator(root, ec)) {
            if (!entry.is_directory(ec)) continue;
            if (auto info = scanItem(entry.path())) items.push_back(*info);
        }
    }
    std::sort(items.begin(), items.end(),
              [](const auto& a, const auto& b) { return a.id < b.id; });
    return items;
}

std::vector<fs::path> WorkshopScanner::defaultRoots() {
    std::vector<fs::path> candidates;
    const char* home = std::getenv("HOME");
    const char* user = std::getenv("USER");
    const std::string tail = "steamapps/workshop/content/431960";
    if (home) {
        candidates.push_back(fs::path(home) / ".local/share/Steam" / tail);
        candidates.push_back(fs::path(home) / ".steam/steam" / tail);
    }
    if (user) {
        std::error_code ec;
        fs::path media = fs::path("/run/media") / user;
        for (const auto& mount : fs::directory_iterator(media, ec)) {
            candidates.push_back(mount.path() / "SteamLibrary" / tail);
        }
    }
    std::vector<fs::path> roots;
    for (const auto& c : candidates) {
        std::error_code ec;
        if (fs::is_directory(c, ec)) roots.push_back(c);
    }
    return roots;
}

std::optional<fs::path> WorkshopScanner::findAssetsDir(
    const std::vector<fs::path>& roots) {
    for (const auto& root : roots) {
        // root = .../steamapps/workshop/content/431960
        fs::path steamapps = root.parent_path().parent_path().parent_path();
        fs::path assets = steamapps / "common/wallpaper_engine/assets";
        std::error_code ec;
        if (fs::is_directory(assets, ec)) return assets;
    }
    return std::nullopt;
}

}  // namespace canvas
