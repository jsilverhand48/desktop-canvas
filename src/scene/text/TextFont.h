// A font for text layers, per src/scene/text/README.md section 2: a
// TrueType or OpenType face loaded with FreeType from the file's bytes,
// with the metrics and glyph bitmaps sections 3 and 6 use. One face per
// object; not thread safe.
//
// systemFontFile() finds a system family's file through fontconfig (the
// `systemfont_<family>` names); without fontconfig at build time it finds
// nothing.
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>

struct FT_LibraryRec_;
struct FT_FaceRec_;

namespace canvas::scene {

class TextFont {
public:
    // bytes must stay readable while the font lives; keep holds them
    // (null when the caller guarantees it). Null with error on failure.
    static std::shared_ptr<TextFont> load(std::span<const uint8_t> bytes, std::shared_ptr<const void> keep,
                                          std::string& error);
    ~TextFont();
    TextFont(const TextFont&) = delete;
    TextFont& operator=(const TextFont&) = delete;

    // The size the metrics, advances and glyphs below refer to, in pixels
    // per em.
    void setPixelSize(float pixels);
    float pixelSize() const { return pixels_; }
    float lineHeight() const;  // the face's line spacing at this size
    float ascender() const;
    // A character's advance: the unhinted advance rounded down to whole
    // pixels (section 3).
    int advance(char32_t c);

    // A character's 8 bit coverage bitmap at this size; rows top first,
    // left and top relative to the pen on the baseline (top upwards).
    struct Glyph {
        int left = 0, top = 0, width = 0, rows = 0, pitch = 0;
        const uint8_t* pixels = nullptr;  // valid until the next glyph call
    };
    bool glyph(char32_t c, Glyph& out);

private:
    TextFont() = default;
    FT_LibraryRec_* library_ = nullptr;
    FT_FaceRec_* face_ = nullptr;
    std::shared_ptr<const void> keep_;
    float pixels_ = 0;
};

std::string systemFontFile(const std::string& family);

}  // namespace canvas::scene
