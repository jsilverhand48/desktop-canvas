// GPU textures for scene rendering, per src/scene/render/README.md
// section 2 and src/scene/assets/README.md sections 3 and 4.
//
// TextureCache::get(name) loads a texture named as materials name them
// ("util/white", "masks/x"; see texturePath) through the asset lookup,
// once: every mip of the first image is decoded and uploaded (DXT stays
// compressed when GL_EXT_texture_compression_s3tc is there, R8 and RG88
// as single and two channel textures, JPEG and PNG as RGBA8 at their
// image size), filtering and wrapping follow the texture flags, volume
// textures become 3D textures. Sprite sheets upload every image and keep
// their frames; spriteFrame() picks the frame for a scene time
// (src/scene/render/README.md section 9). Video textures play through
// VideoTexture; updateVideos() renders their newest frames and points the
// GpuTexture at them (the stand in until the first frame). Missing and
// unsupported textures give a 1x1 transparent black stand in and are
// reported once. Needs the GL context current; owns the textures.
#pragma once

#include <epoxy/gl.h>

#include <array>
#include <map>
#include <string>
#include <vector>

#include "scene/assets/AssetLocator.h"
#include "scene/assets/TextureFile.h"
#include "scene/gl/VideoTexture.h"

namespace canvas::scene {

struct GpuTexture {
    GLuint id = 0;
    GLenum target = GL_TEXTURE_2D;
    int width = 1, height = 1;            // uploaded size of mip 0
    int imageWidth = 1, imageHeight = 1;  // content size
    float uMax = 1, vMax = 1;             // usable region (image / uploaded)
    uint32_t formatCode = 0;
    bool spriteSheet = false;
    // First sprite sheet frame in texture coordinates (g_Texture<n>
    // Translation and Rotation); the whole texture otherwise.
    float frameTranslation[2] = {0, 0};
    float frameRotation[4] = {1, 0, 0, 1};
    bool placeholder = false;  // the stand in
    // Sprite sheets: one texture per image (images[0] is id), each
    // image's uploaded size, the frames and their total duration.
    std::vector<GLuint> images;
    std::vector<std::array<int, 2>> imageSizes;
    std::vector<TextureFrame> frames;
    double sheetDuration = 0;
    bool video = false;  // a VideoTexture feeds id once it has a frame
};

// The frame of a sprite sheet shown at scene time t: the texture to bind
// and g_Texture<n>Translation and Rotation.
struct SpriteFrame {
    GLuint id = 0;
    float translation[2] = {0, 0};
    float rotation[4] = {1, 0, 0, 1};
};
SpriteFrame spriteFrame(const GpuTexture& texture, double t);

class TextureCache {
public:
    explicit TextureCache(const AssetLocator& assets);
    ~TextureCache();
    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    const GpuTexture& get(const std::string& name);
    // Renders the newest frame of every video texture (once per frame).
    void updateVideos();
    const GpuTexture& placeholder() { return placeholder_; }
    // Problems met so far (missing files, unsupported formats).
    const std::vector<std::string>& problems() const { return problems_; }
    bool s3tc() const { return s3tc_; }

private:
    GpuTexture load(const std::string& name);
    // Decodes and uploads one image of a texture; 0 on failure.
    GLuint upload(const TextureFile& tex, size_t image, int& width, int& height,
                  const std::string& name);
    void problem(std::string text);

    const AssetLocator& assets_;
    bool s3tc_ = false;
    GpuTexture placeholder_;
    std::map<std::string, GpuTexture> textures_;
    std::map<std::string, std::unique_ptr<VideoTexture>> videos_;  // by name
    std::vector<std::string> problems_;
};

}  // namespace canvas::scene
