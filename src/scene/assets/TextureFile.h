// .tex textures, per src/scene/assets/README.md section 3: header, image
// container, mips and sprite sheet frames.
//
// parseTexture() only parses: payloads stay views into the bytes it was
// given (a package mapping or a mapped loose file), which must outlive the
// TextureFile. It enforces the section's reader rules (exact consumption,
// size and count limits, known codec); an unknown pixel format code parses
// with `format` empty so the caller can report it. Decoding pixels is
// TextureDecode's job.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace canvas::scene {

// Pixel formats of section 3.3 (header codes in pixelFormatFromCode).
enum class PixelFormat { Rgba8, Dxt5, Dxt3, Dxt1, Rg8, R8 };

std::optional<PixelFormat> pixelFormatFromCode(uint32_t code);
const char* pixelFormatName(PixelFormat format);
bool isBlockCompressed(PixelFormat format);
// Bytes of one width x height slice: block formats count whole 4x4 blocks.
uint64_t sliceBytes(PixelFormat format, uint32_t width, uint32_t height);

// What every mip payload of a texture holds.
enum class PayloadKind { Raw, Jpeg, Png, Video };
const char* payloadKindName(PayloadKind kind);

struct TextureMip {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;  // above 1 only in volume textures
    bool lz4 = false;    // payload is an LZ4 block of decompressedSize bytes
    uint32_t decompressedSize = 0;
    std::span<const uint8_t> payload;
};

struct TextureImage {
    std::vector<TextureMip> mips;  // largest first
};

// One sprite sheet frame: the parallelogram at (x, y) spanned by (ux, uy)
// and (vx, vy), in pixels of images[image].
struct TextureFrame {
    uint32_t image = 0;
    float duration = 0;  // seconds
    float x = 0, y = 0, ux = 0, uy = 0, vx = 0, vy = 0;
};

struct TextureFile {
    // Header flags (section 3.2).
    static constexpr uint32_t kNoInterpolation = 0x1;
    static constexpr uint32_t kClampUVs = 0x2;
    static constexpr uint32_t kSpriteSheet = 0x4;
    static constexpr uint32_t kVideo = 0x20;
    static constexpr uint32_t kVolume = 0x40;

    // Reader limits (section 3.4).
    static constexpr uint32_t kMaxDimension = 16384;
    static constexpr uint32_t kMaxImages = 64;
    static constexpr uint32_t kMaxMips = 16;
    static constexpr uint32_t kMaxDecompressed = 256u << 20;

    uint32_t formatCode = 0;
    std::optional<PixelFormat> format;  // empty for unknown codes
    uint32_t flags = 0;
    uint32_t textureWidth = 0;   // allocated size
    uint32_t textureHeight = 0;
    uint32_t volumeField = 0;    // the extra u32 of volume textures
    uint32_t imageWidth = 0;     // content size
    uint32_t imageHeight = 0;
    uint32_t color = 0;          // purpose unknown
    int containerVersion = 0;    // 1..4 for TEXB0001..TEXB0004
    PayloadKind payload = PayloadKind::Raw;
    std::vector<TextureImage> images;
    int framesVersion = 0;       // 2 or 3 for TEXS0002/TEXS0003, 0 without
    uint32_t frameWidth = 0;     // TEXS0003 only
    uint32_t frameHeight = 0;
    std::vector<TextureFrame> frames;

    bool has(uint32_t flag) const { return (flags & flag) != 0; }
};

// Parses a whole .tex file. On failure returns nullopt and sets error.
std::optional<TextureFile> parseTexture(std::span<const uint8_t> bytes,
                                        std::string& error);

}  // namespace canvas::scene
