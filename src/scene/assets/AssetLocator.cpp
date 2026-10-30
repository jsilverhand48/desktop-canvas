// Implementation of AssetLocator declared in AssetLocator.h.
#include "scene/assets/AssetLocator.h"

#include <charconv>

#include <nlohmann/json.hpp>

#include "core/Log.h"

namespace fs = std::filesystem;

namespace canvas::scene {

namespace {

// A whole string of decimal digits as an unsigned 64 bit number.
std::optional<uint64_t> parseId(std::string_view text) {
    uint64_t value = 0;
    auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || ec != std::errc{} || end != text.data() + text.size())
        return std::nullopt;
    return value;
}

// One path segment that names a file in the override folder itself.
bool plainFileName(const std::string& name) {
    return !name.empty() && name != "." && name != ".." &&
           name.find_first_of("/\\") == std::string::npos;
}

fs::path withoutTrailingSlash(const fs::path& p) {
    fs::path n = p.lexically_normal();
    return n.filename().empty() ? n.parent_path() : n;
}

}  // namespace

const char* AssetLocator::layerName(Layer layer) {
    switch (layer) {
        case Layer::Compat: return "compat";
        case Layer::Loose: return "loose";
        case Layer::Package: return "package";
        case Layer::Assets: return "assets";
    }
    return "?";
}

std::optional<std::string> AssetLocator::normalize(std::string_view path) {
    if (path.find('\0') != std::string_view::npos) return std::nullopt;
    std::string p(path);
    for (char& c : p)
        if (c == '\\') c = '/';
    if (!p.empty() && p.front() == '/') return std::nullopt;
    std::string out;
    size_t start = 0;
    while (start <= p.size()) {
        size_t end = p.find('/', start);
        if (end == std::string::npos) end = p.size();
        std::string_view segment(p.data() + start, end - start);
        if (segment == "..") return std::nullopt;
        if (!segment.empty() && segment != ".") {
            if (!out.empty()) out += '/';
            out += segment;
        }
        start = end + 1;
    }
    if (out.empty()) return std::nullopt;
    return out;
}

std::optional<fs::path> AssetLocator::guessAssetsDir(const fs::path& itemDir) {
    fs::path item = withoutTrailingSlash(itemDir);
    fs::path app = item.parent_path();         // .../content/431960
    fs::path content = app.parent_path();      // .../workshop/content
    fs::path workshop = content.parent_path(); // .../steamapps/workshop
    fs::path steamapps = workshop.parent_path();
    if (app.filename() != "431960" || content.filename() != "content" ||
        workshop.filename() != "workshop")
        return std::nullopt;
    fs::path assets = steamapps / "common" / "wallpaper_engine" / "assets";
    std::error_code ec;
    if (fs::is_directory(assets, ec)) return assets;
    return std::nullopt;
}

bool AssetLocator::open(const fs::path& itemDir, const fs::path& assetsDir,
                        std::string workshopId) {
    itemDir_ = itemDir;
    assetsDir_ = assetsDir;
    workshopId_ = workshopId.empty() ? withoutTrailingSlash(itemDir).filename().string()
                                     : std::move(workshopId);
    hasPackage_ = false;
    overrides_.clear();
    error_.clear();
    std::error_code ec;
    if (!fs::is_directory(itemDir_, ec)) {
        error_ = itemDir_.string() + " is not a folder";
        return false;
    }
    fs::path pkg = itemDir_ / "scene.pkg";
    if (fs::exists(pkg, ec)) {
        if (!package_.open(pkg)) {
            error_ = pkg.string() + ": " + package_.error();
            return false;
        }
        hasPackage_ = true;
    }
    if (!assetsDir_.empty()) loadOverrides();
    return true;
}

void AssetLocator::loadOverrides() {
    // Section 2.1: only numeric workshop ids can be compared with
    // maximumprojectid; local projects are never overridden.
    auto project = parseId(workshopId_);
    if (!project) return;
    std::error_code ec;
    fs::path base = assetsDir_ / "zcompat" / "scene" / "shaders";
    for (fs::directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code dirEc;
        if (!it->is_directory(dirEc)) continue;
        std::string effectId = it->path().filename().string();
        if (!plainFileName(effectId)) continue;
        MappedFile config;
        if (!config.openBeneath(it->path(), "config.json")) continue;
        auto bytes = config.bytes();
        auto json = nlohmann::json::parse(bytes.begin(), bytes.end(), nullptr, false);
        if (json.is_discarded() || !json.is_object()) {
            log::warn() << "ignoring unreadable " << (it->path() / "config.json").string();
            continue;
        }
        bool applies = true;  // a missing maximum means no limit
        if (auto max = json.find("maximumprojectid"); max != json.end()) {
            std::optional<uint64_t> limit;
            if (max->is_string()) limit = parseId(max->get<std::string>());
            else if (max->is_number_unsigned()) limit = max->get<uint64_t>();
            if (!limit) {
                log::warn() << "ignoring " << it->path().string()
                            << ": unparsable maximumprojectid";
                continue;
            }
            applies = *project <= *limit;
        }
        if (!applies) continue;
        Override o;
        o.prefix = "shaders/workshop/" + effectId + "/";
        o.dir = it->path();
        for (const char* key : {"frag", "vert"}) {
            auto file = json.find(key);
            if (file != json.end() && file->is_string() &&
                plainFileName(file->get<std::string>()))
                o.files.push_back(file->get<std::string>());
        }
        if (!o.files.empty()) overrides_.push_back(std::move(o));
    }
}

std::vector<fs::path> AssetLocator::activeOverrides() const {
    std::vector<fs::path> dirs;
    for (const auto& o : overrides_) dirs.push_back(o.dir);
    return dirs;
}

std::optional<AssetLocator::Asset> AssetLocator::mapFile(const fs::path& root,
                                                         const std::string& relative,
                                                         Layer layer) const {
    auto file = std::make_shared<MappedFile>();
    if (!file->openBeneath(root, relative)) return std::nullopt;
    Asset asset;
    asset.bytes = file->bytes();
    asset.layer = layer;
    asset.mapping = std::move(file);
    return asset;
}

std::optional<AssetLocator::Asset> AssetLocator::find(std::string_view path) const {
    auto name = normalize(path);
    if (!name) return std::nullopt;
    for (const auto& o : overrides_) {
        if (name->compare(0, o.prefix.size(), o.prefix) != 0) continue;
        std::string last = name->substr(name->rfind('/') + 1);
        for (const auto& file : o.files)
            if (file == last)
                if (auto asset = mapFile(o.dir, file, Layer::Compat)) return asset;
    }
    if (auto asset = mapFile(itemDir_, *name, Layer::Loose)) return asset;
    if (hasPackage_)
        if (auto bytes = package_.read(*name)) return Asset{*bytes, Layer::Package, nullptr};
    if (!assetsDir_.empty())
        if (auto asset = mapFile(assetsDir_, *name, Layer::Assets)) return asset;
    return std::nullopt;
}

}  // namespace canvas::scene
