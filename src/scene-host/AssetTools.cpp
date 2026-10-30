// Implementation of the asset tools declared in AssetTools.h.
#include "scene-host/AssetTools.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>

#include "core/Log.h"
#include "process/HelperProtocol.h"
#include "scene-host/Screenshot.h"
#include "scene/assets/TextureDecode.h"

namespace fs = std::filesystem;

namespace canvas::scenehost {

namespace {

constexpr int kFailed = 1;

bool endsWith(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Creates path and writes bytes; fails when path already exists (O_EXCL also
// refuses a symlink in its place).
bool writeNew(const fs::path& path, std::span<const uint8_t> bytes, std::string& error) {
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                    0644);
    if (fd < 0) {
        error = strerror(errno);
        return false;
    }
    size_t done = 0;
    while (done < bytes.size()) {
        ssize_t n = ::write(fd, bytes.data() + done, bytes.size() - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            error = strerror(errno);
            ::close(fd);
            return false;
        }
        done += static_cast<size_t>(n);
    }
    if (::close(fd) != 0) {
        error = strerror(errno);
        return false;
    }
    return true;
}

// Writes bytes to path, replacing it (the --extract output is the user's
// choice of file).
bool writeFile(const fs::path& path, std::span<const uint8_t> bytes, std::string& error) {
    fs::path tmp = path;
    tmp += ".tmp";
    std::error_code ec;
    fs::remove(tmp, ec);
    if (!writeNew(tmp, bytes, error)) return false;
    fs::rename(tmp, path, ec);
    if (ec) {
        error = ec.message();
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

int extractTexture(std::span<const uint8_t> bytes, const fs::path& out) {
    std::string error;
    auto texture = scene::parseTexture(bytes, error);
    if (!texture) {
        log::error() << "cannot parse the texture: " << error;
        return kFailed;
    }
    std::cout << describeTexture(*texture).dump(2) << "\n";
    scene::DecodedImage decoded;
    scene::DecodedImage image;
    if (!scene::decodeMip(*texture, texture->images[0].mips[0], decoded, error) ||
        !scene::expandBlocks(decoded, image, error)) {
        log::error() << "cannot decode the texture: " << error;
        return kFailed;
    }
    int channels = scene::channelCount(image.layout);
    std::vector<uint8_t> pixels = std::move(image.pixels);
    if (channels == 2) {  // RG88: write (r, g, 0)
        std::vector<uint8_t> rgb(pixels.size() / 2 * 3);
        for (size_t i = 0, j = 0; i < pixels.size(); i += 2, j += 3) {
            rgb[j] = pixels[i];
            rgb[j + 1] = pixels[i + 1];
            rgb[j + 2] = 0;
        }
        pixels = std::move(rgb);
        channels = 3;
    }
    int height = static_cast<int>(image.height * image.depth);
    if (!writePng(out.string(), static_cast<int>(image.width), height, pixels, channels)) {
        log::error() << "cannot write " << out.string();
        return kFailed;
    }
    return helper::kExitClean;
}

}  // namespace

nlohmann::json describeTexture(const scene::TextureFile& t) {
    using scene::TextureFile;
    nlohmann::json flags = nlohmann::json::array();
    const std::pair<uint32_t, const char*> names[] = {
        {TextureFile::kNoInterpolation, "nointerpolation"},
        {TextureFile::kClampUVs, "clampuvs"},
        {TextureFile::kSpriteSheet, "spritesheet"},
        {TextureFile::kVideo, "video"},
        {TextureFile::kVolume, "volume"},
    };
    uint32_t known = 0;
    for (const auto& [bit, name] : names) {
        if (t.has(bit)) flags.push_back(name);
        known |= bit;
    }
    if (t.flags & ~known) flags.push_back("unknown:" + std::to_string(t.flags & ~known));
    nlohmann::json images = nlohmann::json::array();
    for (const auto& image : t.images) {
        nlohmann::json mips = nlohmann::json::array();
        for (const auto& mip : image.mips) {
            nlohmann::json m = {mip.width, mip.height};
            if (t.has(TextureFile::kVolume)) m.push_back(mip.depth);
            mips.push_back(m);
        }
        images.push_back(mips);
    }
    nlohmann::json j = {
        {"format", t.format ? scene::pixelFormatName(*t.format) : "unknown"},
        {"format_code", t.formatCode},
        {"flags", flags},
        {"texture_size", {t.textureWidth, t.textureHeight}},
        {"image_size", {t.imageWidth, t.imageHeight}},
        {"container", "TEXB000" + std::to_string(t.containerVersion)},
        {"payload", scene::payloadKindName(t.payload)},
        {"images", images},
    };
    if (t.framesVersion) {
        j["frames"] = t.frames.size();
        if (t.framesVersion >= 3) j["frame_size"] = {t.frameWidth, t.frameHeight};
    }
    return j;
}

int extractAsset(const scene::AssetLocator& assets, const std::string& entry,
                 const fs::path& out) {
    auto asset = assets.find(entry);
    if (!asset) {
        log::error() << "no " << entry << " in any lookup layer";
        return kFailed;
    }
    log::info() << entry << " found in layer " << scene::AssetLocator::layerName(asset->layer);
    if (endsWith(entry, ".tex")) return extractTexture(asset->bytes, out);
    std::string error;
    if (!writeFile(out, asset->bytes, error)) {
        log::error() << "cannot write " << out.string() << ": " << error;
        return kFailed;
    }
    return helper::kExitClean;
}

int dumpPackage(const scene::AssetLocator& assets, const fs::path& dir) {
    const scene::PackageArchive* package = assets.package();
    if (!package) {
        log::error() << "no scene.pkg in " << assets.itemDir().string();
        return kFailed;
    }
    std::error_code ec;
    if (fs::exists(dir, ec) && !(fs::is_directory(dir, ec) && fs::is_empty(dir, ec))) {
        log::error() << dir.string() << " must be an empty folder or not exist";
        return helper::kExitUsage;
    }
    fs::create_directories(dir, ec);
    if (ec) {
        log::error() << "cannot create " << dir.string() << ": " << ec.message();
        return kFailed;
    }
    size_t failed = 0;
    for (const auto& entry : package->entries()) {
        // Entry names were validated when the archive opened: relative, no
        // "." or ".." segments, no backslashes.
        fs::path target = dir / entry.name;
        std::string error;
        fs::create_directories(target.parent_path(), ec);
        if (ec || !writeNew(target, *package->read(entry.name), error)) {
            log::error() << "cannot write " << target.string() << ": "
                         << (ec ? ec.message() : error);
            failed++;
            ec.clear();
        }
    }
    log::info() << "wrote " << package->entries().size() - failed << " of "
                << package->entries().size() << " entries to " << dir.string();
    return failed ? kFailed : helper::kExitClean;
}

}  // namespace canvas::scenehost
