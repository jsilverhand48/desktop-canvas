// Compiled and linked GL programs, per src/scene/shader/README.md
// section 8.
//
// request(vertex, fragment) starts making a program for two GLSL sources
// and returns a handle; the same pair always gives the same handle. A
// program comes from memory, else from the disk cache, else from the
// driver. Disk entries live in `directory` (empty: no disk cache), one
// file per pair named by a 64 bit hash of the driver string and both
// sources, holding the driver string, both sources and the program
// binary; a binary is used only when all three match exactly and the
// driver accepts it, so collisions, edited sources and driver updates
// simply compile again (and replace the file). Files are written to a
// temporary name and renamed.
//
// With GL_KHR_parallel_shader_compile the driver compiles on its own
// threads: ready() polls without blocking, result() waits. Without it,
// work happens in request(). The cache owns its programs and deletes them
// on destruction; it needs its GL context current for every call.
#pragma once

#include <epoxy/gl.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace canvas::scene {

class ProgramCache {
public:
    struct Result {
        GLuint program = 0;   // 0 on failure
        std::string error;    // stage and driver log on failure
        bool fromDisk = false;
    };
    using Handle = size_t;

    explicit ProgramCache(std::filesystem::path directory);
    ~ProgramCache();
    ProgramCache(const ProgramCache&) = delete;
    ProgramCache& operator=(const ProgramCache&) = delete;

    Handle request(const std::string& vertex, const std::string& fragment);
    bool ready(Handle handle);
    const Result& result(Handle handle);

    bool parallel() const { return parallel_; }
    bool binaries() const { return binaries_; }
    size_t size() const { return entries_.size(); }

    // GL_VENDOR, GL_RENDERER and GL_VERSION of the current context.
    static std::string driverString();
    // The file an entry would use (tests).
    std::filesystem::path pathFor(const std::string& vertex, const std::string& fragment) const;

private:
    struct Entry {
        std::string vertex, fragment;
        GLuint vs = 0, fs = 0;
        bool done = false;
        Result result;
    };

    bool loadBinary(Entry& entry);
    void storeBinary(const Entry& entry);
    void finish(Entry& entry);

    std::filesystem::path directory_;
    std::string driver_;
    bool parallel_ = false;
    bool binaries_ = false;
    std::vector<Entry> entries_;
    std::map<std::string, Handle> index_;
};

uint64_t fnv1a64(std::string_view data, uint64_t seed = 0xcbf29ce484222325ull);

}  // namespace canvas::scene
