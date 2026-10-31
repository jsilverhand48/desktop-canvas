// Implementation of TextureCache declared in Textures.h.
#include "scene/gl/Textures.h"

#include <cmath>

#include "core/Log.h"
#include "scene/assets/TextureDecode.h"
#include "scene/model/SceneLoader.h"

namespace canvas::scene {

namespace {

GLuint makePlaceholder() {
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    const uint8_t clear[4] = {0, 0, 0, 0};
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, clear);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return id;
}

struct UploadFormat {
    GLenum internal, format, type;
    bool compressed = false;
};

UploadFormat uploadFormat(PixelLayout layout) {
    switch (layout) {
        case PixelLayout::R8: return {GL_R8, GL_RED, GL_UNSIGNED_BYTE};
        case PixelLayout::Rg8: return {GL_RG8, GL_RG, GL_UNSIGNED_BYTE};
        case PixelLayout::Dxt1: return {GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 0, 0, true};
        case PixelLayout::Dxt3: return {GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, 0, 0, true};
        case PixelLayout::Dxt5: return {GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 0, 0, true};
        default: return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
    }
}

}  // namespace

SpriteFrame spriteFrame(const GpuTexture& texture, double t) {
    SpriteFrame out;
    out.id = texture.id;
    std::copy_n(texture.frameTranslation, 2, out.translation);
    std::copy_n(texture.frameRotation, 4, out.rotation);
    if (texture.frames.empty() || !(texture.sheetDuration > 0)) return out;
    double at = std::fmod(std::max(0.0, t), texture.sheetDuration);
    const TextureFrame* frame = &texture.frames.back();
    for (const auto& f : texture.frames) {
        if (at < f.duration) {
            frame = &f;
            break;
        }
        at -= f.duration;
    }
    if (frame->image < texture.images.size() && texture.images[frame->image])
        out.id = texture.images[frame->image];
    const auto& size = frame->image < texture.imageSizes.size() ? texture.imageSizes[frame->image]
                                                                : std::array<int, 2>{1, 1};
    const float w = float(std::max(1, size[0])), h = float(std::max(1, size[1]));
    out.translation[0] = frame->x / w;
    out.translation[1] = frame->y / h;
    out.rotation[0] = frame->ux / w;
    out.rotation[1] = frame->uy / h;
    out.rotation[2] = frame->vx / w;
    out.rotation[3] = frame->vy / h;
    return out;
}

TextureCache::TextureCache(const AssetLocator& assets) : assets_(assets) {
    s3tc_ = epoxy_has_gl_extension("GL_EXT_texture_compression_s3tc");
    placeholder_.id = makePlaceholder();
    placeholder_.placeholder = true;
}

TextureCache::~TextureCache() {
    videos_.clear();  // they own the textures video entries point at
    for (auto& [name, t] : textures_) {
        if (t.placeholder || t.video) continue;
        if (t.images.empty()) glDeleteTextures(1, &t.id);
        for (GLuint id : t.images)
            if (id) glDeleteTextures(1, &id);
    }
    glDeleteTextures(1, &placeholder_.id);
}

void TextureCache::problem(std::string text) {
    log::warn() << text;
    problems_.push_back(std::move(text));
}

void TextureCache::updateVideos() {
    for (auto& [name, video] : videos_) {
        video->update();
        auto it = textures_.find(name);
        if (it == textures_.end() || !video->texture()) continue;
        GpuTexture& t = it->second;
        t.id = video->texture();
        t.width = t.imageWidth = video->width();
        t.height = t.imageHeight = video->height();
        t.uMax = t.vMax = 1;
    }
}

const GpuTexture& TextureCache::get(const std::string& name) {
    auto it = textures_.find(name);
    if (it != textures_.end()) return it->second;
    return textures_.emplace(name, load(name)).first->second;
}

GLuint TextureCache::upload(const TextureFile& tex, size_t index, int& width, int& height,
                            const std::string& name) {
    const TextureImage& image = tex.images[index];
    std::vector<DecodedImage> mips;
    std::string error;
    for (const auto& mip : image.mips) {
        DecodedImage d;
        if (!decodeMip(tex, mip, d, error)) {
            if (mips.empty()) {
                problem("texture " + name + ": " + error);
                return 0;
            }
            break;  // keep the mips that decoded
        }
        if (isBlockLayout(d.layout) && !s3tc_) {
            DecodedImage rgba;
            if (!expandBlocks(d, rgba, error)) break;
            d = std::move(rgba);
        }
        mips.push_back(std::move(d));
    }
    if (mips.empty()) return 0;
    const bool volume = tex.has(TextureFile::kVolume);
    const GLenum target = volume ? GL_TEXTURE_3D : GL_TEXTURE_2D;
    width = int(mips[0].width);
    height = int(mips[0].height);
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(target, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (size_t level = 0; level < mips.size(); level++) {
        const DecodedImage& d = mips[level];
        UploadFormat f = uploadFormat(d.layout);
        GLint l = GLint(level);
        if (volume) {
            glTexImage3D(GL_TEXTURE_3D, l, GLint(f.internal), GLsizei(d.width), GLsizei(d.height),
                         GLsizei(d.depth), 0, f.format, f.type, d.pixels.data());
        } else if (f.compressed) {
            glCompressedTexImage2D(GL_TEXTURE_2D, l, f.internal, GLsizei(d.width), GLsizei(d.height), 0,
                                   GLsizei(d.pixels.size()), d.pixels.data());
        } else {
            glTexImage2D(GL_TEXTURE_2D, l, GLint(f.internal), GLsizei(d.width), GLsizei(d.height), 0,
                         f.format, f.type, d.pixels.data());
        }
    }
    glTexParameteri(target, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, GLint(mips.size()) - 1);
    const bool nearest = tex.has(TextureFile::kNoInterpolation);
    const bool mipmapped = mips.size() > 1;
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER,
                    nearest ? (mipmapped ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST)
                            : (mipmapped ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR));
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    GLint wrap = tex.has(TextureFile::kClampUVs) ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    glTexParameteri(target, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, wrap);
    if (volume) glTexParameteri(target, GL_TEXTURE_WRAP_R, wrap);
    return id;
}

GpuTexture TextureCache::load(const std::string& name) {
    std::string path = texturePath(name);
    auto asset = path.empty() ? std::nullopt : assets_.find(path);
    if (!asset) {
        problem("texture " + name + ": not found");
        return placeholder_;
    }
    std::string error;
    auto tex = parseTexture(asset->bytes, error);
    if (!tex) {
        problem("texture " + name + ": " + error);
        return placeholder_;
    }
    if (tex->payload == PayloadKind::Video) {
        // The MP4 is the first mip's payload (section 9), read in place:
        // the package mapping lives as long as the asset lookup, and a
        // loose file's mapping is kept by the texture.
        const TextureMip& mip = tex->images.front().mips.front();
        std::span<const uint8_t> bytes = mip.payload;
        std::shared_ptr<const void> keep = asset->mapping;
        if (mip.lz4) {
            auto unpacked = std::make_shared<std::vector<uint8_t>>();
            if (!decompressLz4(mip.payload, mip.decompressedSize, *unpacked, error)) {
                problem("texture " + name + ": " + error);
                return placeholder_;
            }
            bytes = *unpacked;
            keep = unpacked;
        }
        auto video = VideoTexture::open(bytes, std::move(keep), error);
        if (!video) {
            problem("texture " + name + ": " + error);
            return placeholder_;
        }
        videos_[name] = std::move(video);
        GpuTexture t = placeholder_;
        t.placeholder = false;
        t.video = true;
        return t;
    }
    GpuTexture t;
    t.formatCode = tex->formatCode;
    t.spriteSheet = tex->has(TextureFile::kSpriteSheet);
    const bool volume = tex->has(TextureFile::kVolume);
    t.target = volume ? GL_TEXTURE_3D : GL_TEXTURE_2D;
    t.id = upload(*tex, 0, t.width, t.height, name);
    if (!t.id) return placeholder_;
    // Raw payloads are stored at the texture size; the image is the
    // content in its top left corner (rows top first).
    bool padded = tex->payload == PayloadKind::Raw && !volume;
    t.imageWidth = padded ? int(std::min(tex->imageWidth, uint32_t(t.width))) : t.width;
    t.imageHeight = padded ? int(std::min(tex->imageHeight, uint32_t(t.height))) : t.height;
    t.uMax = float(t.imageWidth) / float(t.width);
    t.vMax = float(t.imageHeight) / float(t.height);
    if (t.spriteSheet && !tex->frames.empty()) {
        // Every image of the sheet, then the frames (section 9).
        t.images.push_back(t.id);
        t.imageSizes.push_back({t.width, t.height});
        for (size_t i = 1; i < tex->images.size(); i++) {
            int w = 1, h = 1;
            t.images.push_back(upload(*tex, i, w, h, name));
            t.imageSizes.push_back({w, h});
        }
        t.frames = tex->frames;
        for (const auto& f : t.frames) t.sheetDuration += std::max(0.0f, f.duration);
        SpriteFrame first = spriteFrame(t, 0);
        std::copy_n(first.translation, 2, t.frameTranslation);
        std::copy_n(first.rotation, 4, t.frameRotation);
    }
    return t;
}

}  // namespace canvas::scene
