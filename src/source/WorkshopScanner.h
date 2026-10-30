// Scans Wallpaper Engine workshop content directories.
//
// A content root is a directory shaped like
//   .../steamapps/workshop/content/431960
// holding one subdirectory per workshop item, each with a project.json.
// The scanner is tolerant by design: malformed JSON, missing files, or
// unknown types produce Unsupported entries with a reason instead of errors,
// because the daemon must keep running whatever the library contains.
//
// The scanner also derives the Wallpaper Engine assets directory (shipped
// with the Windows app under steamapps/common/wallpaper_engine/assets) which
// linux-wallpaperengine needs to render scenes.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "source/WallpaperInfo.h"

namespace canvas {

class WorkshopScanner {
public:
    // Scans every root and returns items sorted by id. Roots that do not
    // exist (e.g. removable media not mounted) are skipped silently.
    static std::vector<WallpaperInfo> scan(
        const std::vector<std::filesystem::path>& roots);

    // Parses one item directory. Returns nullopt only when project.json is
    // absent; any parse problem still yields an Unsupported entry.
    static std::optional<WallpaperInfo> scanItem(
        const std::filesystem::path& itemDir);

    // Well known Steam library locations that contain workshop content for
    // app 431960: ~/.local/share/Steam, ~/.steam/steam, and any
    // /run/media/<user>/*/SteamLibrary mount. Only existing paths are
    // returned.
    static std::vector<std::filesystem::path> defaultRoots();
    // defaultRoots() for an explicit home and user. Looks in every Steam
    // install (steamInstalls), every library its
    // steamapps/libraryfolders.vdf lists, and /run/media/<user>/*/SteamLibrary;
    // returns the workshop content dirs that exist, without duplicates
    // (~/.steam/steam is usually a link to ~/.local/share/Steam).
    static std::vector<std::filesystem::path> defaultRootsFor(
        const std::filesystem::path& home, const std::string& user);
    // Steam client locations under home: native (~/.local/share/Steam,
    // ~/.steam/steam), Flatpak (~/.var/app/com.valvesoftware.Steam/...)
    // and snap (~/snap/steam/common/.local/share/Steam).
    static std::vector<std::filesystem::path> steamInstalls(
        const std::filesystem::path& home);
    // Library folders ("path" entries) in a libraryfolders.vdf text.
    static std::vector<std::filesystem::path> parseLibraryFolders(
        const std::string& vdf);

    // Given a content root, walks up to steamapps and looks for
    // common/wallpaper_engine/assets in the same Steam library.
    static std::optional<std::filesystem::path> findAssetsDir(
        const std::vector<std::filesystem::path>& roots);
};

}  // namespace canvas
