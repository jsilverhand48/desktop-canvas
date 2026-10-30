// Bounds checked little endian reader over a byte span.
//
// Every read checks the remaining length first; a failed read sets the
// reader's error (with the offset and what was being read) and returns a
// zero value, after which all further reads fail too, so a parser can run a
// whole sequence of reads and check ok() once at the end. Nothing here
// allocates except error messages and string reads. Workshop files are
// untrusted input: no read may run past the span.
#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

namespace canvas::scene {

class ByteReader {
public:
    explicit ByteReader(std::span<const uint8_t> data) : data_(data) {}

    bool ok() const { return error_.empty(); }
    const std::string& error() const { return error_; }
    size_t offset() const { return pos_; }
    size_t remaining() const { return ok() ? data_.size() - pos_ : 0; }
    bool atEnd() const { return ok() && pos_ == data_.size(); }

    uint32_t u32(const char* what) {
        uint8_t b[4];
        if (!take(b, 4, what)) return 0;
        return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 |
               uint32_t(b[3]) << 24;
    }
    int32_t i32(const char* what) { return static_cast<int32_t>(u32(what)); }
    float f32(const char* what) {
        uint32_t bits = u32(what);
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    // count raw bytes as a view into the underlying data (empty on failure).
    std::span<const uint8_t> bytes(size_t count, const char* what) {
        if (!require(count, what)) return {};
        auto view = data_.subspan(pos_, count);
        pos_ += count;
        return view;
    }
    std::string string(size_t count, const char* what) {
        auto view = bytes(count, what);
        return std::string(reinterpret_cast<const char*>(view.data()), view.size());
    }
    // An 8 character tag followed by NUL ("TEXB0004\0"); empty on failure.
    std::string tag(const char* what) {
        auto view = bytes(9, what);
        if (view.empty()) return {};
        if (view[8] != 0) {
            fail(what, "tag is not NUL terminated");
            return {};
        }
        return std::string(reinterpret_cast<const char*>(view.data()), 8);
    }

    void fail(const char* what, std::string_view why) {
        if (!ok()) return;
        error_ = std::string(what) + " at offset " + std::to_string(pos_) + ": " +
                 std::string(why);
    }

private:
    bool require(size_t count, const char* what) {
        if (!ok()) return false;
        if (count > data_.size() - pos_) {
            fail(what, "needs " + std::to_string(count) + " bytes, " +
                           std::to_string(data_.size() - pos_) + " left");
            return false;
        }
        return true;
    }
    bool take(uint8_t* out, size_t count, const char* what) {
        if (!require(count, what)) return false;
        std::memcpy(out, data_.data() + pos_, count);
        pos_ += count;
        return true;
    }

    std::span<const uint8_t> data_;
    size_t pos_ = 0;
    std::string error_;
};

}  // namespace canvas::scene
