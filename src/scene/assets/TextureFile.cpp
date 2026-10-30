// Implementation of the .tex parser declared in TextureFile.h.
#include "scene/assets/TextureFile.h"

#include <cmath>

#include "scene/assets/ByteReader.h"

namespace canvas::scene {

std::optional<PixelFormat> pixelFormatFromCode(uint32_t code) {
    switch (code) {
        case 0: return PixelFormat::Rgba8;
        case 4: return PixelFormat::Dxt5;
        case 6: return PixelFormat::Dxt3;
        case 7: return PixelFormat::Dxt1;
        case 8: return PixelFormat::Rg8;
        case 9: return PixelFormat::R8;
        default: return std::nullopt;
    }
}

const char* pixelFormatName(PixelFormat format) {
    switch (format) {
        case PixelFormat::Rgba8: return "RGBA8888";
        case PixelFormat::Dxt5: return "DXT5";
        case PixelFormat::Dxt3: return "DXT3";
        case PixelFormat::Dxt1: return "DXT1";
        case PixelFormat::Rg8: return "RG88";
        case PixelFormat::R8: return "R8";
    }
    return "?";
}

bool isBlockCompressed(PixelFormat format) {
    return format == PixelFormat::Dxt1 || format == PixelFormat::Dxt3 ||
           format == PixelFormat::Dxt5;
}

uint64_t sliceBytes(PixelFormat format, uint32_t width, uint32_t height) {
    uint64_t w = width;
    uint64_t h = height;
    switch (format) {
        case PixelFormat::Rgba8: return w * h * 4;
        case PixelFormat::Rg8: return w * h * 2;
        case PixelFormat::R8: return w * h;
        case PixelFormat::Dxt1: return ((w + 3) / 4) * ((h + 3) / 4) * 8;
        case PixelFormat::Dxt3:
        case PixelFormat::Dxt5: return ((w + 3) / 4) * ((h + 3) / 4) * 16;
    }
    return 0;
}

const char* payloadKindName(PayloadKind kind) {
    switch (kind) {
        case PayloadKind::Raw: return "raw";
        case PayloadKind::Jpeg: return "jpeg";
        case PayloadKind::Png: return "png";
        case PayloadKind::Video: return "mp4";
    }
    return "?";
}

namespace {

// "PREFIX000N" tags: N when the tag is prefix + 000 + one of the digits
// allowed, else 0.
int tagVersion(const std::string& tag, const char* prefix, int lowest, int highest) {
    std::string p(prefix);
    if (tag.size() != 8 || tag.compare(0, p.size(), p) != 0) return 0;
    std::string digits = tag.substr(p.size());
    for (char c : digits)
        if (c < '0' || c > '9') return 0;
    int v = std::stoi(digits);
    return v >= lowest && v <= highest ? v : 0;
}

bool dimensionOk(uint32_t v) { return v >= 1 && v <= TextureFile::kMaxDimension; }

}  // namespace

std::optional<TextureFile> parseTexture(std::span<const uint8_t> bytes,
                                        std::string& error) {
    TextureFile t;
    ByteReader in(bytes);
    auto fail = [&](const char* what, const std::string& why) {
        in.fail(what, why);
        error = in.error();
        return std::nullopt;
    };

    std::string version = in.tag("texture version");
    if (in.ok() && version != "TEXV0005")
        return fail("texture version", "unsupported '" + version + "'");
    std::string info = in.tag("texture info");
    if (in.ok() && info != "TEXI0001")
        return fail("texture info", "unsupported '" + info + "'");
    t.formatCode = in.u32("format");
    t.format = pixelFormatFromCode(t.formatCode);
    t.flags = in.u32("flags");
    t.textureWidth = in.u32("texture width");
    t.textureHeight = in.u32("texture height");
    if (t.has(TextureFile::kVolume)) t.volumeField = in.u32("volume field");
    t.imageWidth = in.u32("image width");
    t.imageHeight = in.u32("image height");
    t.color = in.u32("color");
    if (in.ok() && !(dimensionOk(t.textureWidth) && dimensionOk(t.textureHeight) &&
                     dimensionOk(t.imageWidth) && dimensionOk(t.imageHeight)))
        return fail("header", "texture or image size out of range");

    std::string container = in.tag("image container");
    if (in.ok() && !(t.containerVersion = tagVersion(container, "TEXB", 1, 4)))
        return fail("image container", "unsupported '" + container + "'");
    uint32_t count = in.u32("image count");
    if (in.ok() && (count == 0 || count > TextureFile::kMaxImages))
        return fail("image count", std::to_string(count) + " images");
    int32_t codec = -1;
    if (t.containerVersion >= 3) codec = in.i32("codec");
    if (t.containerVersion >= 4) in.u32("container field");
    switch (codec) {
        case -1:
            t.payload = t.has(TextureFile::kVideo) ? PayloadKind::Video : PayloadKind::Raw;
            break;
        case 2: t.payload = PayloadKind::Jpeg; break;
        case 13: t.payload = PayloadKind::Png; break;
        default:
            if (in.ok()) return fail("codec", "unknown codec " + std::to_string(codec));
    }

    for (uint32_t i = 0; in.ok() && i < count; i++) {
        TextureImage image;
        uint32_t mips = in.u32("mip count");
        if (in.ok() && (mips == 0 || mips > TextureFile::kMaxMips))
            return fail("mip count", std::to_string(mips) + " mips");
        for (uint32_t m = 0; in.ok() && m < mips; m++) {
            TextureMip mip;
            mip.width = in.u32("mip width");
            mip.height = in.u32("mip height");
            if (t.has(TextureFile::kVolume)) mip.depth = in.u32("mip depth");
            if (t.containerVersion >= 2) {
                mip.lz4 = in.u32("lz4 flag") != 0;
                mip.decompressedSize = in.u32("decompressed size");
            }
            uint32_t size = in.u32("payload size");
            mip.payload = in.bytes(size, "payload");
            if (!in.ok()) break;
            if (!dimensionOk(mip.width) || !dimensionOk(mip.height) ||
                !dimensionOk(mip.depth))
                return fail("mip", "size out of range");
            if (mip.lz4 && mip.decompressedSize > TextureFile::kMaxDecompressed)
                return fail("mip", "decompressed size above 256 MiB");
            image.mips.push_back(mip);
        }
        t.images.push_back(std::move(image));
    }

    if (in.ok() && t.has(TextureFile::kSpriteSheet)) {
        std::string frames = in.tag("frames version");
        if (in.ok() && !(t.framesVersion = tagVersion(frames, "TEXS", 2, 3)))
            return fail("frames version", "unsupported '" + frames + "'");
        uint32_t n = in.u32("frame count");
        if (t.framesVersion >= 3) {
            t.frameWidth = in.u32("frame width");
            t.frameHeight = in.u32("frame height");
        }
        if (in.ok() && n > in.remaining() / 32)
            return fail("frame count", std::to_string(n) + " frames do not fit");
        t.frames.reserve(n);
        for (uint32_t i = 0; in.ok() && i < n; i++) {
            TextureFrame f;
            f.image = in.u32("frame image");
            f.duration = in.f32("frame duration");
            f.x = in.f32("frame x");
            f.y = in.f32("frame y");
            f.ux = in.f32("frame ux");
            f.uy = in.f32("frame uy");
            f.vx = in.f32("frame vx");
            f.vy = in.f32("frame vy");
            if (!in.ok()) break;
            if (f.image >= t.images.size())
                return fail("frame", "names image " + std::to_string(f.image) + " of " +
                                         std::to_string(t.images.size()));
            for (float v : {f.duration, f.x, f.y, f.ux, f.uy, f.vx, f.vy})
                if (!std::isfinite(v)) return fail("frame", "value is not finite");
            t.frames.push_back(f);
        }
    }

    if (in.ok() && !in.atEnd())
        return fail("end", std::to_string(in.remaining()) + " bytes left over");
    if (!in.ok()) {
        error = in.error();
        return std::nullopt;
    }
    error.clear();
    return t;
}

}  // namespace canvas::scene
