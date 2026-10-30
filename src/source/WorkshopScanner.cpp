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
#include <iterator>
#include <regex>

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
    if (project.contains("contentrating") && project["contentrating"].is_string())
        info.contentRating = project["contentrating"].get<std::string>();
    if (project.contains("tags") && project["tags"].is_array())
        for (const auto& t : project["tags"])
            if (t.is_string()) info.tags.push_back(t.get<std::string>());

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
    const char* home = std::getenv("HOME");
    const char* user = std::getenv("USER");
    return defaultRootsFor(home ? fs::path(home) : fs::path(),
                           user ? std::string(user) : std::string());
}

std::vector<fs::path> WorkshopScanner::steamInstalls(const fs::path& home) {
    if (home.empty()) return {};
    return {home / ".local/share/Steam", home / ".steam/steam",
            home / ".var/app/com.valvesoftware.Steam/.local/share/Steam",
            home / "snap/steam/common/.local/share/Steam"};
}

std::vector<fs::path> WorkshopScanner::parseLibraryFolders(const std::string& vdf) {
    // Lines look like:   "path"   "/run/media/user/disk/SteamLibrary"
    // Values escape backslashes and quotes with a backslash.
    std::vector<fs::path> folders;
    static const std::regex kPath("\"path\"\\s+\"((?:[^\"\\\\]|\\\\.)*)\"",
                                  std::regex::icase);
    for (auto it = std::sregex_iterator(vdf.begin(), vdf.end(), kPath);
         it != std::sregex_iterator(); ++it) {
        std::string raw = (*it)[1].str();
        std::string value;
        for (size_t i = 0; i < raw.size(); i++) {
            if (raw[i] == '\\' && i + 1 < raw.size()) i++;
            value += raw[i];
        }
        if (!value.empty()) folders.emplace_back(value);
    }
    return folders;
}

std::vector<fs::path> WorkshopScanner::defaultRootsFor(const fs::path& home,
                                                       const std::string& user) {
    const std::string tail = "steamapps/workshop/content/431960";
    std::vector<fs::path> candidates;
    for (const auto& install : steamInstalls(home)) {
        candidates.push_back(install / tail);
        std::ifstream vdf(install / "steamapps/libraryfolders.vdf");
        if (!vdf) continue;
        std::string text((std::istreambuf_iterator<char>(vdf)),
                         std::istreambuf_iterator<char>());
        for (const auto& library : parseLibraryFolders(text))
            candidates.push_back(library / tail);
    }
    if (!user.empty()) {
        std::error_code ec;
        fs::path media = fs::path("/run/media") / user;
        for (const auto& mount : fs::directory_iterator(media, ec))
            candidates.push_back(mount.path() / "SteamLibrary" / tail);
    }
    std::vector<fs::path> roots;
    std::vector<fs::path> seen;  // canonical forms
    for (const auto& c : candidates) {
        std::error_code ec;
        if (!fs::is_directory(c, ec)) continue;
        fs::path canonical = fs::weakly_canonical(c, ec);
        if (ec) canonical = c;
        if (std::find(seen.begin(), seen.end(), canonical) != seen.end()) continue;
        seen.push_back(canonical);
        roots.push_back(c);
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
