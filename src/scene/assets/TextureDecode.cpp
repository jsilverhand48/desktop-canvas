// Implementation of the decoders declared in TextureDecode.h.
#include "scene/assets/TextureDecode.h"

#include <lz4.h>
#include <png.h>
#include <turbojpeg.h>

#include <climits>
#include <cstring>
#include <memory>

namespace canvas::scene {

int channelCount(PixelLayout layout) {
    switch (layout) {
        case PixelLayout::R8: return 1;
        case PixelLayout::Rg8: return 2;
        default: return 4;
    }
}

bool isBlockLayout(PixelLayout layout) {
    return layout == PixelLayout::Dxt1 || layout == PixelLayout::Dxt3 ||
           layout == PixelLayout::Dxt5;
}

bool decompressLz4(std::span<const uint8_t> block, size_t size,
                   std::vector<uint8_t>& out, std::string& error) {
    if (size > kMaxDecodedBytes || block.size() > static_cast<size_t>(INT_MAX)) {
        error = "LZ4 block too large";
        return false;
    }
    out.resize(size);
    int got = LZ4_decompress_safe(reinterpret_cast<const char*>(block.data()),
                                  reinterpret_cast<char*>(out.data()),
                                  static_cast<int>(block.size()), static_cast<int>(size));
    if (got < 0 || static_cast<size_t>(got) != size) {
        error = got < 0 ? "corrupt LZ4 block"
                        : "LZ4 block expands to " + std::to_string(got) + " bytes, not " +
                              std::to_string(size);
        out.clear();
        return false;
    }
    return true;
}

namespace {

PixelLayout layoutFor(PixelFormat format) {
    switch (format) {
        case PixelFormat::Rgba8: return PixelLayout::Rgba8;
        case PixelFormat::Dxt5: return PixelLayout::Dxt5;
        case PixelFormat::Dxt3: return PixelLayout::Dxt3;
        case PixelFormat::Dxt1: return PixelLayout::Dxt1;
        case PixelFormat::Rg8: return PixelLayout::Rg8;
        case PixelFormat::R8: return PixelLayout::R8;
    }
    return PixelLayout::Rgba8;
}

std::string sizeText(uint32_t w, uint32_t h) {
    return std::to_string(w) + "x" + std::to_string(h);
}

// Uses the TurboJPEG 2.x interface (libjpeg-turbo 2.1 and later; 3.x keeps
// it), so distributions still on 2.1 build this too.
bool decodeJpeg(std::span<const uint8_t> data, uint32_t width, uint32_t height,
                std::vector<uint8_t>& out, std::string& error) {
    std::unique_ptr<void, int (*)(tjhandle)> handle(tjInitDecompress(), tjDestroy);
    if (!handle) {
        error = "cannot create a JPEG decoder";
        return false;
    }
    tjhandle tj = handle.get();
    int w = 0;
    int h = 0;
    int subsampling = 0;
    int colorspace = 0;
    if (tjDecompressHeader3(tj, data.data(), static_cast<unsigned long>(data.size()), &w, &h,
                            &subsampling, &colorspace) != 0) {
        error = std::string("bad JPEG: ") + tjGetErrorStr2(tj);
        return false;
    }
    if (w != static_cast<int>(width) || h != static_cast<int>(height)) {
        error = "JPEG is " + std::to_string(w) + "x" + std::to_string(h) +
                ", mip says " + sizeText(width, height);
        return false;
    }
    if (uint64_t(width) * height * 4 > kMaxDecodedBytes) {  // checked before allocating
        error = "JPEG too large";
        return false;
    }
    out.resize(size_t(width) * height * 4);
    // TJFLAG_LIMITSCANS bounds the work a hostile progressive JPEG can cause.
    if (tjDecompress2(tj, data.data(), static_cast<unsigned long>(data.size()), out.data(), w,
                      0, h, TJPF_RGBA, TJFLAG_LIMITSCANS) != 0 &&
        tjGetErrorCode(tj) != TJERR_WARNING) {  // warnings still yield an image
        error = std::string("bad JPEG: ") + tjGetErrorStr2(tj);
        out.clear();
        return false;
    }
    return true;
}

bool decodePng(std::span<const uint8_t> data, uint32_t width, uint32_t height,
               std::vector<uint8_t>& out, std::string& error) {
    png_image image;
    std::memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, data.data(), data.size())) {
        error = std::string("bad PNG: ") + image.message;
        png_image_free(&image);
        return false;
    }
    if (image.width != width || image.height != height) {
        error = "PNG is " + sizeText(image.width, image.height) + ", mip says " +
                sizeText(width, height);
        png_image_free(&image);
        return false;
    }
    if (uint64_t(width) * height * 4 > kMaxDecodedBytes) {
        error = "PNG too large";
        png_image_free(&image);
        return false;
    }
    image.format = PNG_FORMAT_RGBA;
    out.resize(PNG_IMAGE_SIZE(image));
    if (!png_image_finish_read(&image, nullptr, out.data(), 0, nullptr)) {
        error = std::string("bad PNG: ") + image.message;
        png_image_free(&image);
        out.clear();
        return false;
    }
    return true;
}

// --- S3TC blocks (Direct3D BC1 to BC3 rules) -------------------------------

struct Rgba {
    uint8_t r, g, b, a;
};

Rgba expand565(uint16_t c) {
    uint8_t r = (c >> 11) & 0x1f;
    uint8_t g = (c >> 5) & 0x3f;
    uint8_t b = c & 0x1f;
    return {uint8_t((r << 3) | (r >> 2)), uint8_t((g << 2) | (g >> 4)),
            uint8_t((b << 3) | (b >> 2)), 255};
}

uint8_t mixChannel(int a, int b, int wa, int wb) {
    int total = wa + wb;
    return static_cast<uint8_t>((a * wa + b * wb + total / 2) / total);
}

Rgba mixColor(Rgba a, Rgba b, int wa, int wb) {
    return {mixChannel(a.r, b.r, wa, wb), mixChannel(a.g, b.g, wa, wb),
            mixChannel(a.b, b.b, wa, wb), 255};
}

// The four colours of an 8 byte colour block. With allowThreeColor (DXT1
// only), colour 0 <= colour 1 selects three colours plus transparent black;
// DXT3 and DXT5 always interpolate four. Mixes round to nearest.
void colorPalette(const uint8_t* block, bool allowThreeColor, Rgba palette[4]) {
    uint16_t c0 = uint16_t(block[0] | block[1] << 8);
    uint16_t c1 = uint16_t(block[2] | block[3] << 8);
    palette[0] = expand565(c0);
    palette[1] = expand565(c1);
    if (allowThreeColor && c0 <= c1) {
        palette[2] = mixColor(palette[0], palette[1], 1, 1);
        palette[3] = {0, 0, 0, 0};
    } else {
        palette[2] = mixColor(palette[0], palette[1], 2, 1);
        palette[3] = mixColor(palette[0], palette[1], 1, 2);
    }
}

uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
           uint32_t(p[3]) << 24;
}

// Decodes one 4x4 block into texels[16] (row major).
void decodeBlock(PixelLayout layout, const uint8_t* block, Rgba texels[16]) {
    const uint8_t* color = layout == PixelLayout::Dxt1 ? block : block + 8;
    Rgba palette[4];
    colorPalette(color, layout == PixelLayout::Dxt1, palette);
    uint32_t indices = le32(color + 4);
    for (int i = 0; i < 16; i++) texels[i] = palette[(indices >> (2 * i)) & 3];

    if (layout == PixelLayout::Dxt3) {
        for (int i = 0; i < 16; i++) {
            uint8_t nibble = (block[i / 2] >> ((i & 1) * 4)) & 0xf;
            texels[i].a = uint8_t(nibble * 17);
        }
    } else if (layout == PixelLayout::Dxt5) {
        int a0 = block[0];
        int a1 = block[1];
        uint8_t alpha[8] = {uint8_t(a0), uint8_t(a1)};
        if (a0 > a1) {
            for (int k = 1; k <= 6; k++) alpha[k + 1] = mixChannel(a0, a1, 7 - k, k);
        } else {
            for (int k = 1; k <= 4; k++) alpha[k + 1] = mixChannel(a0, a1, 5 - k, k);
            alpha[6] = 0;
            alpha[7] = 255;
        }
        uint64_t bits = 0;
        for (int i = 0; i < 6; i++) bits |= uint64_t(block[2 + i]) << (8 * i);
        for (int i = 0; i < 16; i++) texels[i].a = alpha[(bits >> (3 * i)) & 7];
    }
}

}  // namespace

bool decodeImageFile(std::span<const uint8_t> data, DecodedImage& out, std::string& error) {
    out = DecodedImage{};
    if (data.size() >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
        std::unique_ptr<void, int (*)(tjhandle)> handle(tjInitDecompress(), tjDestroy);
        int w = 0, h = 0, subsampling = 0, colorspace = 0;
        if (!handle || tjDecompressHeader3(handle.get(), data.data(), static_cast<unsigned long>(data.size()), &w, &h,
                                           &subsampling, &colorspace) != 0 || w <= 0 || h <= 0) {
            error = "bad JPEG";
            return false;
        }
        out.width = uint32_t(w);
        out.height = uint32_t(h);
        return decodeJpeg(data, out.width, out.height, out.pixels, error);
    }
    if (data.size() >= 8 && std::memcmp(data.data(), "\x89PNG", 4) == 0) {
        png_image image;
        std::memset(&image, 0, sizeof(image));
        image.version = PNG_IMAGE_VERSION;
        if (!png_image_begin_read_from_memory(&image, data.data(), data.size())) {
            error = "bad PNG";
            png_image_free(&image);
            return false;
        }
        out.width = image.width;
        out.height = image.height;
        png_image_free(&image);
        return decodePng(data, out.width, out.height, out.pixels, error);
    }
    error = "not a JPEG or PNG file";
    return false;
}

bool decodeMip(const TextureFile& texture, const TextureMip& mip, DecodedImage& out,
               std::string& error) {
    out = DecodedImage{};
    out.width = mip.width;
    out.height = mip.height;
    out.depth = mip.depth;
    // Volume slices are stacked vertically in one image. Both factors are at
    // most kMaxDimension (parser rule), so this fits in 32 bits.
    const uint32_t stackedHeight = mip.height * mip.depth;
    switch (texture.payload) {
        case PayloadKind::Video:
            error = "MP4 payloads go to the video texture path";
            return false;
        case PayloadKind::Jpeg:
        case PayloadKind::Png: {
            std::span<const uint8_t> data = mip.payload;
            std::vector<uint8_t> expanded;
            if (mip.lz4) {
                if (!decompressLz4(mip.payload, mip.decompressedSize, expanded, error))
                    return false;
                data = expanded;
            }
            out.layout = PixelLayout::Rgba8;
            return texture.payload == PayloadKind::Jpeg
                       ? decodeJpeg(data, mip.width, stackedHeight, out.pixels, error)
                       : decodePng(data, mip.width, stackedHeight, out.pixels, error);
        }
        case PayloadKind::Raw:
            break;
    }
    if (!texture.format) {
        error = "unsupported pixel format code " + std::to_string(texture.formatCode);
        return false;
    }
    out.layout = layoutFor(*texture.format);
    uint64_t expected = sliceBytes(*texture.format, mip.width, mip.height) * mip.depth;
    if (expected > kMaxDecodedBytes) {
        error = "decoded mip above 256 MiB";
        return false;
    }
    if (mip.lz4) {
        if (mip.decompressedSize != expected) {
            error = "LZ4 size " + std::to_string(mip.decompressedSize) + " for a " +
                    sizeText(mip.width, mip.height) + " " +
                    pixelFormatName(*texture.format) + " mip, expected " +
                    std::to_string(expected);
            return false;
        }
        return decompressLz4(mip.payload, expected, out.pixels, error);
    }
    if (mip.payload.size() != expected) {
        error = "payload has " + std::to_string(mip.payload.size()) + " bytes, a " +
                sizeText(mip.width, mip.height) + " " +
                pixelFormatName(*texture.format) + " mip needs " +
                std::to_string(expected);
        return false;
    }
    out.pixels.assign(mip.payload.begin(), mip.payload.end());
    return true;
}

bool expandBlocks(const DecodedImage& in, DecodedImage& out, std::string& error) {
    if (!isBlockLayout(in.layout)) {
        out = in;
        return true;
    }
    const uint32_t bw = (in.width + 3) / 4;
    const uint32_t bh = (in.height + 3) / 4;
    const size_t blockBytes = in.layout == PixelLayout::Dxt1 ? 8 : 16;
    const uint64_t slice = uint64_t(bw) * bh * blockBytes;
    if (in.pixels.size() != slice * in.depth) {
        error = "block data does not match the image size";
        return false;
    }
    const uint64_t rgbaSlice = uint64_t(in.width) * in.height * 4;
    if (rgbaSlice * in.depth > kMaxDecodedBytes) {
        error = "expanded image above 256 MiB";
        return false;
    }
    DecodedImage result;
    result.layout = PixelLayout::Rgba8;
    result.width = in.width;
    result.height = in.height;
    result.depth = in.depth;
    result.pixels.resize(rgbaSlice * in.depth);
    Rgba texels[16];
    for (uint32_t z = 0; z < in.depth; z++) {
        const uint8_t* src = in.pixels.data() + z * slice;
        uint8_t* dst = result.pixels.data() + z * rgbaSlice;
        for (uint32_t by = 0; by < bh; by++) {
            for (uint32_t bx = 0; bx < bw; bx++, src += blockBytes) {
                decodeBlock(in.layout, src, texels);
                for (uint32_t ty = 0; ty < 4; ty++) {
                    uint32_t y = by * 4 + ty;
                    if (y >= in.height) break;
                    for (uint32_t tx = 0; tx < 4; tx++) {
                        uint32_t x = bx * 4 + tx;
                        if (x >= in.width) break;
                        const Rgba& t = texels[ty * 4 + tx];
                        uint8_t* p = dst + (size_t(y) * in.width + x) * 4;
                        p[0] = t.r;
                        p[1] = t.g;
                        p[2] = t.b;
                        p[3] = t.a;
                    }
                }
            }
        }
    }
    out = std::move(result);
    return true;
}

}  // namespace canvas::scene
