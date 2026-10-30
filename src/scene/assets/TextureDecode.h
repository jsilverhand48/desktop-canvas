// Pixel decoding for .tex mips, per src/scene/assets/README.md section 4.
//
// decodeMip() turns one parsed mip into tightly packed rows, top to bottom.
// Raw payloads (LZ4 expanded when flagged) keep their pixel format, so DXT
// data stays compressed for the GL upload; JPEG and PNG payloads decode to
// RGBA8. A payload whose size or decoded dimensions differ from the mip
// fields is an error, and no decoded buffer may exceed kMaxDecodedBytes.
// MP4 payloads are refused: they belong to the video texture path (phase
// P4b). expandBlocks() turns DXT1/3/5 into RGBA8 on the CPU for
// screenshots, tests, --extract and drivers without S3TC. Nothing here
// touches GL, and nothing keeps state, so decoding may run on any thread.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "scene/assets/TextureFile.h"

namespace canvas::scene {

enum class PixelLayout { R8, Rg8, Rgba8, Dxt1, Dxt3, Dxt5 };

// Channels per pixel: 1, 2 or 4 (DXT counts as the 4 it expands to).
int channelCount(PixelLayout layout);
bool isBlockLayout(PixelLayout layout);

struct DecodedImage {
    PixelLayout layout = PixelLayout::Rgba8;
    uint32_t width = 0;
    uint32_t height = 0;  // of one slice
    uint32_t depth = 1;   // volume slices, stored one after another
    std::vector<uint8_t> pixels;
};

inline constexpr uint64_t kMaxDecodedBytes = 256ull << 20;

// Decompresses an LZ4 block (no frame header) that must expand to exactly
// size bytes.
bool decompressLz4(std::span<const uint8_t> block, size_t size,
                   std::vector<uint8_t>& out, std::string& error);

bool decodeMip(const TextureFile& texture, const TextureMip& mip,
               DecodedImage& out, std::string& error);

// RGBA8 copy of a DXT image (Direct3D BC1 to BC3 rules, see the README);
// other layouts are copied unchanged.
bool expandBlocks(const DecodedImage& in, DecodedImage& out, std::string& error);

}  // namespace canvas::scene
