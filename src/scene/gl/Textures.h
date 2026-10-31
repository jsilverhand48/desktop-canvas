// GPU textures for scene rendering, per src/scene/render/README.md
// section 2 and src/scene/assets/README.md sections 3 and 4.
//
// TextureCache::get(name) loads a texture named as materials name them
// ("util/white", "masks/x"; see texturePath) through the asset lookup,
// once: every mip of the first image is decoded and uploaded (DXT stays
// compressed when GL_EXT_texture_compression_s3tc is there, R8 and RG88
// as single and two channel textures, JPEG and PNG as RGBA8 at their
// image size), filtering and wrapping follow the texture flags, volume
// textures become 3D textures. Missing, video and unsupported textures
// give a 1x1 transparent black stand in and are reported once. Needs the
// GL context current; owns the textures.
#pragma once

#include <epoxy/gl.h>

#include <map>
#include <string>
#include <vector>

#include "scene/assets/AssetLocator.h"

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
    // Translation and Rotation); the whole texture otherwise. Animation
    // is phase P4b.
    float frameTranslation[2] = {0, 0};
    float frameRotation[4] = {1, 0, 0, 1};
    bool placeholder = false;  // the stand in
};

class TextureCache {
public:
    explicit TextureCache(const AssetLocator& assets);
    ~TextureCache();
    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    const GpuTexture& get(const std::string& name);
    const GpuTexture& placeholder() { return placeholder_; }
    // Problems met so far (missing files, unsupported formats).
    const std::vector<std::string>& problems() const { return problems_; }
    bool s3tc() const { return s3tc_; }

private:
    GpuTexture load(const std::string& name);
    void problem(std::string text);

    const AssetLocator& assets_;
    bool s3tc_ = false;
    GpuTexture placeholder_;
    std::map<std::string, GpuTexture> textures_;
    std::vector<std::string> problems_;
};

}  // namespace canvas::scene
