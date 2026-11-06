// Implementation of the support checks declared in SceneCheck.h.
#include "scene-host/SceneCheck.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <map>

#include "process/HelperProtocol.h"
#include "scene/assets/AssetLocator.h"
#include "scene/gl/SoundOutputMpv.h"
#include "scene/gl/VideoTexture.h"
#include "scene/model/Capabilities.h"
#include "scene/model/Json.h"
#include "scene/model/SceneLoader.h"

namespace fs = std::filesystem;

namespace canvas::scenehost {

Examination examine(const fs::path& wallpaper, const fs::path& assetsDir,
                    const scene::PropertyValues& overrides) {
    Examination exam;
    fs::path assets = assetsDir;
    if (assets.empty())
        if (auto guess = scene::AssetLocator::guessAssetsDir(wallpaper)) assets = *guess;
    scene::AssetLocator locator;
    scene::LoadedScene loaded;
    if (!locator.open(wallpaper, assets)) {
        loaded.report.error(locator.error());
    } else {
        exam.loaded = scene::loadScene(locator, overrides, loaded);
        if (exam.loaded) exam.features = scene::sceneFeatures(loaded);
    }
    exam.report = scene::checkReport(wallpaper.string(), exam.loaded, exam.features, loaded.report);
    // Video textures and sound layers need libmpv in this build.
    if (exam.features.count("video-texture") && !scene::VideoTexture::available()) {
        exam.report["unsupported"].push_back("video-texture");
        exam.report["supported"] = false;
    }
    if (exam.features.count("sound") && !scene::SoundOutputMpv::available()) {
        exam.report["unsupported"].push_back("sound");
        exam.report["supported"] = false;
    }
    exam.unsupported = exam.report["unsupported"].get<std::vector<std::string>>();
    return exam;
}

namespace {

// The project type of a folder's project.json, lower cased; empty when it
// has none.
std::string projectType(const fs::path& dir) {
    scene::MappedFile file;
    if (!file.open(dir / "project.json")) return {};
    std::string error;
    auto json = scene::parseJson(file.bytes(), error);
    if (!json || !json->is_object()) return {};
    auto type = json->find("type");
    if (type == json->end() || !type->is_string()) return {};
    std::string t = type->get<std::string>();
    std::transform(t.begin(), t.end(), t.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return t;
}

}  // namespace

int checkAll(const fs::path& root, const fs::path& assetsDir, bool json) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        std::cerr << "desktop-canvas-scene: " << root.string() << " is not a folder\n";
        return helper::kExitUsage;
    }
    std::vector<fs::path> items;
    for (const auto& entry : fs::directory_iterator(root, ec))
        if (entry.is_directory(ec) && projectType(entry.path()) == "scene")
            items.push_back(entry.path());
    std::sort(items.begin(), items.end());

    fs::path assets = assetsDir;
    if (assets.empty() && !items.empty())
        if (auto guess = scene::AssetLocator::guessAssetsDir(items.front())) assets = *guess;

    nlohmann::json all = nlohmann::json::array();
    std::map<std::string, size_t> featureCounts;
    size_t loaded = 0, supported = 0, withErrors = 0;
    for (const auto& item : items) {
        Examination exam = examine(item, assets, {});
        loaded += exam.loaded;
        supported += exam.unsupported.empty();
        withErrors += !exam.report["errors"].empty();
        for (const auto& f : exam.features) featureCounts[f]++;
        if (json) {
            exam.report["id"] = item.filename().string();
            all.push_back(std::move(exam.report));
        }
    }
    if (json) {
        std::cout << all.dump(2) << "\n";
        return helper::kExitClean;
    }
    std::cout << items.size() << " scenes, " << loaded << " loaded, " << supported
              << " fully supported, " << withErrors << " with file errors\n";
    for (const auto& [feature, count] : featureCounts)
        std::cout << "  " << feature << " " << count << "\n";
    return helper::kExitClean;
}

}  // namespace canvas::scenehost
