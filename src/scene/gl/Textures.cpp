// Implementation of TextureCache declared in Textures.h.
#include "scene/gl/Textures.h"

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

TextureCache::TextureCache(const AssetLocator& assets) : assets_(assets) {
    s3tc_ = epoxy_has_gl_extension("GL_EXT_texture_compression_s3tc");
    placeholder_.id = makePlaceholder();
    placeholder_.placeholder = true;
}

TextureCache::~TextureCache() {
    for (auto& [name, t] : textures_)
        if (!t.placeholder) glDeleteTextures(1, &t.id);
    glDeleteTextures(1, &placeholder_.id);
}

void TextureCache::problem(std::string text) {
    log::warn() << text;
    problems_.push_back(std::move(text));
}

const GpuTexture& TextureCache::get(const std::string& name) {
    auto it = textures_.find(name);
    if (it != textures_.end()) return it->second;
    return textures_.emplace(name, load(name)).first->second;
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
        problem("texture " + name + ": video textures arrive in phase P4b");
        return placeholder_;
    }
    const TextureImage& image = tex->images.front();
    std::vector<DecodedImage> mips;
    for (const auto& mip : image.mips) {
        DecodedImage d;
        if (!decodeMip(*tex, mip, d, error)) {
            if (mips.empty()) {
                problem("texture " + name + ": " + error);
                return placeholder_;
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
    GpuTexture t;
    t.formatCode = tex->formatCode;
    t.spriteSheet = tex->has(TextureFile::kSpriteSheet);
    const bool volume = tex->has(TextureFile::kVolume);
    t.target = volume ? GL_TEXTURE_3D : GL_TEXTURE_2D;
    t.width = int(mips[0].width);
    t.height = int(mips[0].height);
    // Raw payloads are stored at the texture size; the image is the
    // content in its top left corner (rows top first).
    bool padded = tex->payload == PayloadKind::Raw && !volume;
    t.imageWidth = padded ? int(std::min(tex->imageWidth, uint32_t(t.width))) : t.width;
    t.imageHeight = padded ? int(std::min(tex->imageHeight, uint32_t(t.height))) : t.height;
    t.uMax = float(t.imageWidth) / float(t.width);
    t.vMax = float(t.imageHeight) / float(t.height);
    if (t.spriteSheet && !tex->frames.empty()) {
        const TextureFrame& f = tex->frames.front();
        float w = float(t.width), h = float(t.height);
        t.frameTranslation[0] = f.x / w;
        t.frameTranslation[1] = f.y / h;
        t.frameRotation[0] = f.ux / w;
        t.frameRotation[1] = f.uy / h;
        t.frameRotation[2] = f.vx / w;
        t.frameRotation[3] = f.vy / h;
    }

    glGenTextures(1, &t.id);
    glBindTexture(t.target, t.id);
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
    glTexParameteri(t.target, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(t.target, GL_TEXTURE_MAX_LEVEL, GLint(mips.size()) - 1);
    const bool nearest = tex->has(TextureFile::kNoInterpolation);
    const bool mipmapped = mips.size() > 1;
    glTexParameteri(t.target, GL_TEXTURE_MIN_FILTER,
                    nearest ? (mipmapped ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST)
                            : (mipmapped ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR));
    glTexParameteri(t.target, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    GLint wrap = tex->has(TextureFile::kClampUVs) ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    glTexParameteri(t.target, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(t.target, GL_TEXTURE_WRAP_T, wrap);
    if (volume) glTexParameteri(t.target, GL_TEXTURE_WRAP_R, wrap);
    return t;
}

}  // namespace canvas::scene
