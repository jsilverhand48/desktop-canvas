// Implementation of ProgramCache declared in ProgramCache.h.
#include "scene/gl/ProgramCache.h"

#include <cstdio>
#include <fstream>
#include <iterator>

#include "core/Log.h"

namespace fs = std::filesystem;

namespace canvas::scene {

namespace {

constexpr char kMagic[8] = {'D', 'C', 'P', 'B', '0', '0', '0', '1'};
constexpr uint32_t kMaxSection = 64u << 20;

void put32(std::string& out, uint32_t v) {
    for (int i = 0; i < 4; i++) out.push_back(static_cast<char>(v >> (8 * i)));
}

bool get32(const std::string& in, size_t& pos, uint32_t& v) {
    if (pos + 4 > in.size()) return false;
    v = 0;
    for (int i = 0; i < 4; i++) v |= uint32_t(static_cast<unsigned char>(in[pos + size_t(i)])) << (8 * i);
    pos += 4;
    return true;
}

bool getSection(const std::string& in, size_t& pos, std::string& out) {
    uint32_t length = 0;
    if (!get32(in, pos, length) || length > kMaxSection || pos + length > in.size()) return false;
    out.assign(in, pos, length);
    pos += length;
    return true;
}

std::string shaderLog(GLuint shader) {
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string log(size_t(length > 0 ? length : 1), '\0');
    glGetShaderInfoLog(shader, length, nullptr, log.data());
    if (!log.empty() && log.back() == '\0') log.pop_back();
    return log;
}

std::string programLog(GLuint program) {
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::string log(size_t(length > 0 ? length : 1), '\0');
    glGetProgramInfoLog(program, length, nullptr, log.data());
    if (!log.empty() && log.back() == '\0') log.pop_back();
    return log;
}

GLuint compile(GLenum type, const std::string& source) {
    GLuint shader = glCreateShader(type);
    const char* text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    return shader;
}

}  // namespace

uint64_t fnv1a64(std::string_view data, uint64_t seed) {
    uint64_t h = seed;
    for (unsigned char c : data) {
        h ^= c;
        h *= 0x100000001b3ull;
    }
    return h;
}

std::string ProgramCache::driverString() {
    auto text = [](GLenum name) {
        const GLubyte* s = glGetString(name);
        return s ? std::string(reinterpret_cast<const char*>(s)) : std::string("?");
    };
    return text(GL_VENDOR) + "\n" + text(GL_RENDERER) + "\n" + text(GL_VERSION);
}

ProgramCache::ProgramCache(fs::path directory) : directory_(std::move(directory)) {
    driver_ = driverString();
    parallel_ = epoxy_has_gl_extension("GL_KHR_parallel_shader_compile");
    if (parallel_) glMaxShaderCompilerThreadsKHR(0xFFFFFFFFu);  // driver's choice
    GLint formats = 0;
    if (epoxy_gl_version() >= 41 || epoxy_has_gl_extension("GL_ARB_get_program_binary"))
        glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &formats);
    binaries_ = formats > 0 && !directory_.empty();
    if (binaries_) {
        std::error_code ec;
        fs::create_directories(directory_, ec);
        if (ec) binaries_ = false;
    }
}

ProgramCache::~ProgramCache() {
    for (auto& e : entries_) {
        if (e.vs) glDeleteShader(e.vs);
        if (e.fs) glDeleteShader(e.fs);
        if (e.result.program) glDeleteProgram(e.result.program);
    }
}

fs::path ProgramCache::pathFor(const std::string& vertex, const std::string& fragment) const {
    uint64_t h = fnv1a64(driver_);
    h = fnv1a64(std::string_view("\0", 1), h);
    h = fnv1a64(vertex, h);
    h = fnv1a64(std::string_view("\0", 1), h);
    h = fnv1a64(fragment, h);
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx.bin", static_cast<unsigned long long>(h));
    return directory_ / name;
}

ProgramCache::Handle ProgramCache::request(const std::string& vertex, const std::string& fragment) {
    std::string key = vertex;
    key.push_back('\0');
    key += fragment;
    if (auto it = index_.find(key); it != index_.end()) return it->second;
    Handle handle = entries_.size();
    index_.emplace(std::move(key), handle);
    entries_.push_back(Entry{vertex, fragment});
    Entry& e = entries_.back();
    if (binaries_ && loadBinary(e)) {
        e.done = true;
        return handle;
    }
    e.vs = compile(GL_VERTEX_SHADER, vertex);
    e.fs = compile(GL_FRAGMENT_SHADER, fragment);
    e.result.program = glCreateProgram();
    glAttachShader(e.result.program, e.vs);
    glAttachShader(e.result.program, e.fs);
    if (binaries_) glProgramParameteri(e.result.program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
    glLinkProgram(e.result.program);
    if (!parallel_) finish(e);
    return handle;
}

bool ProgramCache::ready(Handle handle) {
    Entry& e = entries_.at(handle);
    if (e.done) return true;
    GLint complete = GL_TRUE;
    glGetProgramiv(e.result.program, GL_COMPLETION_STATUS_KHR, &complete);
    if (!complete) return false;
    finish(e);
    return true;
}

const ProgramCache::Result& ProgramCache::result(Handle handle) {
    Entry& e = entries_.at(handle);
    if (!e.done) finish(e);
    return e.result;
}

void ProgramCache::finish(Entry& e) {
    GLint linked = GL_FALSE;
    glGetProgramiv(e.result.program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint ok = GL_FALSE;
        glGetShaderiv(e.vs, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            e.result.error = "vertex shader: " + shaderLog(e.vs);
        } else {
            glGetShaderiv(e.fs, GL_COMPILE_STATUS, &ok);
            e.result.error = ok ? "link: " + programLog(e.result.program)
                                : "fragment shader: " + shaderLog(e.fs);
        }
        glDeleteProgram(e.result.program);
        e.result.program = 0;
    } else if (binaries_) {
        storeBinary(e);
    }
    glDeleteShader(e.vs);
    glDeleteShader(e.fs);
    e.vs = e.fs = 0;
    e.done = true;
}

bool ProgramCache::loadBinary(Entry& e) {
    fs::path path = pathFor(e.vertex, e.fragment);
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    size_t pos = sizeof(kMagic);
    std::string driver, vertex, fragment, binary;
    uint32_t format = 0;
    bool valid = data.size() > pos && data.compare(0, pos, kMagic, pos) == 0 &&
                 getSection(data, pos, driver) && getSection(data, pos, vertex) &&
                 getSection(data, pos, fragment) && get32(data, pos, format) &&
                 getSection(data, pos, binary) && pos == data.size();
    if (!valid || driver != driver_ || vertex != e.vertex || fragment != e.fragment) return false;
    GLuint program = glCreateProgram();
    glProgramBinary(program, format, binary.data(), static_cast<GLsizei>(binary.size()));
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {  // the driver changed its mind: compile again
        glDeleteProgram(program);
        return false;
    }
    e.result.program = program;
    e.result.fromDisk = true;
    return true;
}

void ProgramCache::storeBinary(const Entry& e) {
    GLint length = 0;
    glGetProgramiv(e.result.program, GL_PROGRAM_BINARY_LENGTH, &length);
    if (length <= 0 || uint32_t(length) > kMaxSection) return;
    std::string binary(size_t(length), '\0');
    GLenum format = 0;
    GLsizei written = 0;
    glGetProgramBinary(e.result.program, length, &written, &format, binary.data());
    if (written <= 0) return;
    binary.resize(size_t(written));
    std::string data(kMagic, sizeof(kMagic));
    const std::string* sections[] = {&driver_, &e.vertex, &e.fragment};
    for (const std::string* s : sections) {
        put32(data, static_cast<uint32_t>(s->size()));
        data += *s;
    }
    put32(data, format);
    put32(data, static_cast<uint32_t>(binary.size()));
    data += binary;
    fs::path path = pathFor(e.vertex, e.fragment);
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(data.data(), std::streamsize(data.size()));
        if (!out) {
            log::warn() << "cannot write shader cache entry " << tmp.string();
            return;
        }
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) fs::remove(tmp, ec);
}

}  // namespace canvas::scene
