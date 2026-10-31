// Implementation of SceneRenderer declared in SceneRenderer.h. Section
// numbers refer to src/scene/render/README.md.
#include "scene/gl/SceneRenderer.h"

#include <epoxy/gl.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <list>
#include <map>
#include <optional>
#include <regex>
#include <set>

#include "core/Log.h"
#include "scene/gl/ProgramCache.h"
#include "scene/gl/Textures.h"
#include "scene/model/Json.h"
#include "scene/shader/Programs.h"
#include "scene/shader/Translator.h"

namespace canvas::scene {

Clamp clampFromName(const std::string& name) {
    if (name == "border") return Clamp::Border;
    if (name == "repeat") return Clamp::Repeat;
    return Clamp::Clamp;
}

bool RenderDebug::parse(const std::string& list, std::string& error) {
    size_t start = 0;
    while (start <= list.size()) {
        size_t comma = list.find(',', start);
        std::string item = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        start = comma == std::string::npos ? list.size() + 1 : comma + 1;
        if (item.empty()) continue;
        auto number = [&](const std::string& text, int64_t& out) {
            try {
                size_t used = 0;
                out = std::stoll(text, &used);
                return used == text.size();
            } catch (...) {
                return false;
            }
        };
        size_t eq = item.find('=');
        std::string key = item.substr(0, eq);
        std::string value = eq == std::string::npos ? "" : item.substr(eq + 1);
        int64_t id = 0;
        if (item == "pass-log") {
            passLog = true;
        } else if (item == "base-only") {
            baseOnly = true;
        } else if (item == "no-bloom") {
            noBloom = true;
        } else if (item == "no-solid-final") {
            keepAlpha = true;
        } else if (key == "dump-programs" && !value.empty()) {
            dumpPrograms = value;
        } else if (key == "object" && number(value, id)) {
            only.insert(id);
        } else if (key == "skip-object" && number(value, id)) {
            skip.insert(id);
        } else if (key == "skip-effect") {
            size_t colon = value.find(':');
            int64_t index = -1;
            if (!number(value.substr(0, colon), id) ||
                (colon != std::string::npos && !number(value.substr(colon + 1), index))) {
                error = "bad --render-debug item '" + item + "'";
                return false;
            }
            skipEffects.insert({id, int(index)});
        } else {
            error = "unknown --render-debug item '" + item + "'";
            return false;
        }
    }
    return true;
}

namespace {

constexpr int kSlots = kMaxTextureSlots;
constexpr int kMaxBufferSize = 8192;
constexpr uint64_t kUnusedFrames = 120;  // pooled buffers idle this long are freed

const char* const kPassthroughMaterial = "materials/util/effectpassthrough.json";
const char* const kComposeBackgroundMaterial = "materials/util/effectcomposebackground.json";

// Wallpaper Engine's bloom chain (section 5): material, target buffer
// (empty: the output) and that buffer's size divisor.
struct BloomStep {
    const char* material;
    const char* target;
    int divisor;
};
constexpr BloomStep kBloomSteps[] = {
    {"materials/util/downsample_quarter_bloom.json", "_rt_4FrameBuffer", 4},
    {"materials/util/downsample_eighth_blur_v.json", "_rt_8FrameBuffer", 8},
    {"materials/util/blur_h_bloom.json", "_rt_Bloom", 8},
    {"materials/util/combine_ldr.json", "", 1},
};

// Every projection is followed by this, so buffers hold their content top
// row first as on Direct3D (section 1).
Mat4 flipY() { return Mat4::scale(1, -1, 1); }

bool equalsIgnoreCase(const std::string& a, const std::string& b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) ==
                      std::tolower(static_cast<unsigned char>(y));
           });
}

// ----------------------------------------------------------- render targets

struct Target {
    GLuint fbo = 0, texture = 0;
    int width = 0, height = 0;
    GLenum internal = GL_RGBA8;
    int levels = 1;
};

GLenum baseFormat(GLenum internal) {
    if (internal == GL_R8) return GL_RED;
    if (internal == GL_RG8) return GL_RG;
    return GL_RGBA;
}

void createTarget(Target& t, int width, int height, GLenum internal, bool mipmapped = false) {
    t.width = std::clamp(width, 1, kMaxBufferSize);
    t.height = std::clamp(height, 1, kMaxBufferSize);
    t.internal = internal;
    t.levels = 1;
    if (mipmapped)
        for (int m = std::max(t.width, t.height); m > 1; m >>= 1) t.levels++;
    glGenTextures(1, &t.texture);
    glBindTexture(GL_TEXTURE_2D, t.texture);
    int w = t.width, h = t.height;
    for (int level = 0; level < t.levels; level++) {
        glTexImage2D(GL_TEXTURE_2D, level, GLint(internal), w, h, 0, baseFormat(internal),
                     GL_UNSIGNED_BYTE, nullptr);
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, t.levels - 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    mipmapped ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &t.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.texture, 0);
}

void destroyTarget(Target& t) {
    if (t.fbo) glDeleteFramebuffers(1, &t.fbo);
    if (t.texture) glDeleteTextures(1, &t.texture);
    t = Target{};
}

void clearTarget(const Target& t) {
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glViewport(0, 0, t.width, t.height);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
}

void copyTarget(const Target& from, const Target& to) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, from.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, to.fbo);
    glBlitFramebuffer(0, 0, from.width, from.height, 0, 0, to.width, to.height,
                      GL_COLOR_BUFFER_BIT,
                      from.width == to.width && from.height == to.height ? GL_NEAREST : GL_LINEAR);
}

// Buffers that live for one layer's effects; reused by size and format.
class TargetPool {
public:
    ~TargetPool() {
        for (auto& s : slots_) destroyTarget(s.target);
    }

    Target* acquire(int width, int height, GLenum internal, uint64_t now) {
        width = std::clamp(width, 1, kMaxBufferSize);
        height = std::clamp(height, 1, kMaxBufferSize);
        for (auto& s : slots_)
            if (!s.inUse && s.target.width == width && s.target.height == height &&
                s.target.internal == internal) {
                s.inUse = true;
                s.lastUse = now;
                return &s.target;
            }
        slots_.emplace_back();
        Slot& s = slots_.back();
        createTarget(s.target, width, height, internal);
        s.inUse = true;
        s.lastUse = now;
        return &s.target;
    }

    void release(const Target* target) {
        for (auto& s : slots_)
            if (&s.target == target) s.inUse = false;
    }

    void collect(uint64_t now) {
        for (auto it = slots_.begin(); it != slots_.end();) {
            if (!it->inUse && now - it->lastUse > kUnusedFrames) {
                destroyTarget(it->target);
                it = slots_.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    struct Slot {
        Target target;
        bool inUse = false;
        uint64_t lastUse = 0;
    };
    std::list<Slot> slots_;  // stable addresses
};

// ----------------------------------------------------------------- programs

struct Constant {
    std::string uniform;   // GLSL name
    std::string material;  // the annotation's "material" name
    std::string type;      // float, vec2, vec3, vec4, int, uint, bool
    GLint location = -1;
    Floats fallback;       // the annotation's "default"
};

// Engine uniforms (section 5) a program declares.
struct EngineLocations {
    GLint mvp = -1, mvpInverse = -1, effectMvp = -1, effectMvpInverse = -1, model = -1,
          modelInverse = -1, effectModel = -1, viewProjection = -1, normalModel = -1,
          time = -1, frameTime = -1, screen = -1, texelSize = -1, texelSizeHalf = -1,
          pointer = -1, pointerLast = -1, parallax = -1, color4 = -1, color = -1, alpha = -1,
          eye = -1;
    std::array<GLint, kSlots> resolution, rotation, translation, mipInfo;
    EngineLocations() {
        resolution.fill(-1);
        rotation.fill(-1);
        translation.fill(-1);
        mipInfo.fill(-1);
    }
};

struct Program {
    std::string where;  // the first pass that asked for it, for problems
    ShaderInterface interface;
    std::map<std::string, int> combos;
    ProgramCache::Handle handle = 0;
    bool requested = false;
    bool usable = false;  // linked and set up
    GLuint id = 0;
    EngineLocations engine;
    std::array<GLenum, kSlots> sampler{};  // texture target per declared slot
    std::array<std::string, kSlots> samplerDefault;
    std::vector<Constant> constants;
    bool spriteSheet = false;  // SPRITESHEET: the quad carries 0..1 frame coordinates
};

std::optional<int> textureSlot(const std::string& name, const char* prefix, const char* suffix) {
    size_t p = std::char_traits<char>::length(prefix);
    size_t s = std::char_traits<char>::length(suffix);
    if (name.size() <= p + s || name.compare(0, p, prefix) != 0 ||
        name.compare(name.size() - s, s, suffix) != 0)
        return std::nullopt;
    std::string digits = name.substr(p, name.size() - p - s);
    if (digits.empty() || digits.size() > 2 ||
        !std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return std::nullopt;
    int slot = std::stoi(digits);
    if (slot >= kSlots) return std::nullopt;
    return slot;
}

Floats floatsOf(const nlohmann::json& v) {
    Floats f;
    if (v.is_number()) {
        f.v[0] = v.get<float>();
        f.count = 1;
    } else if (v.is_boolean()) {
        f.v[0] = v.get<bool>() ? 1.0f : 0.0f;
        f.count = 1;
    } else if (v.is_string()) {
        f.count = textToFloats(v.get<std::string>(), f.v.data(), 4);
    } else if (v.is_array()) {
        for (const auto& e : v) {
            if (f.count == 4 || !e.is_number()) break;
            f.v[size_t(f.count++)] = e.get<float>();
        }
    }
    return f;
}

std::string programKey(const ProgramRequest& r) {
    std::string key = r.shader + "|";
    for (const auto& [name, value] : r.combos) key += name + "=" + std::to_string(value) + ";";
    key += "|";
    for (int i = 0; i < kSlots; i++)
        if (r.textureSet[size_t(i)])
            key += std::to_string(i) + ":" + std::to_string(r.textureFormat[size_t(i)]) + ",";
    return key;
}

// ------------------------------------------------------------------- passes

enum class SlotKind : uint8_t { Empty, Texture, Previous, Buffer, Frame, MipFrame, Layer, Composite };

struct Slot {
    SlotKind kind = SlotKind::Empty;
    const GpuTexture* texture = nullptr;  // Texture
    std::string name;                     // Buffer: effect fbo name
    int64_t object = 0;                   // Composite: the layer's object id
};

// _rt_imageLayerComposite_<id>_a or _b: image layer <id>'s composite
// buffers (section 3); nullopt for other names.
std::optional<int64_t> compositeObject(const std::string& name) {
    static const std::regex pattern(R"(_rt_imageLayerComposite_(-?\d{1,18})_[ab])");
    std::smatch m;
    if (!std::regex_match(name, m, pattern)) return std::nullopt;
    return std::stoll(m[1].str());
}

enum class Blend : uint8_t { Normal, Translucent, Additive };

Blend blendOf(const std::string& name) {
    if (name == "translucent") return Blend::Translucent;
    if (name == "additive") return Blend::Additive;
    return Blend::Normal;
}

// Where one constant's value comes from, first found wins (section 3).
struct ConstantSource {
    const Field<Floats>* instance = nullptr;  // scene.json effect pass settings
    const Field<Floats>* material = nullptr;  // the material's constantshadervalues
    std::string property;                     // usershadervalues binding
    bool objectBrightness = false, objectAlpha = false;  // genericimage2 defaults
    // Bloom passes: scene.json general values, when present.
    const Field<float>* sceneFloat = nullptr;
    const Field<Vec3>* sceneVec3 = nullptr;
};

struct Pass {
    std::string where;
    Program* program = nullptr;
    Blend blend = Blend::Normal;
    bool alphaWrite = true;
    std::array<Slot, kSlots> slots;
    std::vector<ConstantSource> sources;  // parallel to program->constants
    // Effect passes.
    std::string target;  // fbo name; empty: the effect's output
    bool copy = false;   // command "copy" from source to target
    std::string source;
    bool needsFrame = false, needsMipFrame = false;

    bool usable() const { return copy || (program && program->usable); }
};

struct EffectBuffer {
    std::string name;
    GLenum internal = GL_RGBA8;
    int divisor = 1;
    std::optional<int64_t> width, height;
    bool unique = false;
    Target persistent;  // unique buffers keep their pixels between frames
};

struct EffectRun {
    const EffectInstance* instance = nullptr;
    std::string where;
    std::vector<Pass> passes;
    std::vector<EffectBuffer> buffers;
    bool usable = true;
};

enum class LayerKind : uint8_t { Image, Solid, Compose, Fullscreen };

struct Layer {
    size_t object = 0;
    int64_t id = 0;  // the object's id
    LayerKind kind = LayerKind::Image;
    const ModelFile* model = nullptr;
    const GpuTexture* texture = nullptr;  // the material's slot 0 texture
    std::vector<Pass> passes;             // the layer's own material
    std::vector<EffectRun> effects;
    Pass base;  // effectpassthrough or effectcomposebackground (image layers)
    bool hasBase = false;
    bool usable = true;
    // Another layer samples this one's composite: it is computed every
    // frame, shown or not, and kept until the frame ends.
    bool referenced = false;
    const Target* composite = nullptr;
};

// Vertices of one quad as a triangle strip: x, y, z, u, v each.
using Quad = std::array<float, 20>;

// The layer rectangle; texture v 0 at its top (section 1).
Quad layerQuad(const LayerRect& r, float uMax, float vMax) {
    return {r.x0, r.y0, 0, 0,    vMax,  //
            r.x1, r.y0, 0, uMax, vMax,  //
            r.x0, r.y1, 0, 0,    0,     //
            r.x1, r.y1, 0, uMax, 0};
}

// Clip space, texture (0,0) on row 0 (fullscreen layers).
Quad clipQuad() {
    return {-1, -1, 0, 0, 0,  //
            1,  -1, 0, 1, 0,  //
            -1, 1,  0, 0, 1,  //
            1,  1,  0, 1, 1};
}

const char* const kBlitVertex = R"(#version 330 core
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_TexCoord;
uniform mat4 u_Matrix;
out vec2 v_TexCoord;
void main() {
    gl_Position = u_Matrix * vec4(a_Position, 1.0);
    v_TexCoord = a_TexCoord;
}
)";

const char* const kBlitFragment = R"(#version 330 core
in vec2 v_TexCoord;
uniform sampler2D u_Texture;
uniform float u_Opaque;
out vec4 o_Color;
void main() {
    o_Color = texture(u_Texture, v_TexCoord);
    o_Color.a = mix(o_Color.a, 1.0, u_Opaque);
}
)";

GLuint compileStage(GLenum stage, const char* source) {
    GLuint s = glCreateShader(stage);
    glShaderSource(s, 1, &source, nullptr);
    glCompileShader(s);
    return s;
}

}  // namespace

// --------------------------------------------------------------------- Impl

struct SceneRenderer::Impl {
    Impl(const AssetLocator& a, Options o)
        : assets(a), options(std::move(o)), programs(options.shaderCache), textures(a),
          loader(shaderLoader(a)) {}

    ~Impl() {
        for (auto& layer : layers)
            for (auto& run : layer.effects)
                for (auto& b : run.buffers) destroyTarget(b.persistent);
        for (auto& f : frames) {
            destroyTarget(f.target);
            destroyTarget(f.copy);
            destroyTarget(f.mipCopy);
        }
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        if (blitProgram) glDeleteProgram(blitProgram);
    }

    struct Frame {
        Target target, copy, mipCopy;
        uint64_t generation = ~0ull;
        uint64_t lastUse = 0;
        bool copyValid = false, mipValid = false;
    };

    // What one draw needs besides the pass.
    struct DrawInputs {
        const Target* destination = nullptr;  // null: the frame
        Mat4 mvp = Mat4::identity();
        Mat4 effectMvp = Mat4::identity();
        Mat4 model = Mat4::identity();
        Quad quad{};
        const Target* previous = nullptr;   // an effect's input
        const Target* override0 = nullptr;  // replaces slot 0 (effect result)
        const GpuTexture* layerTexture = nullptr;
        const std::map<std::string, const Target*>* named = nullptr;
        const SceneObject* object = nullptr;
        bool white = false;  // base and effect passes: g_Color4 is white
    };

    const AssetLocator& assets;
    Options options;
    LoadedScene scene;
    std::unique_ptr<SceneLayout> layout;
    ProgramCache programs;
    TextureCache textures;
    ShaderLoader loader;
    std::map<std::string, std::unique_ptr<Program>> programIndex;
    std::vector<Layer> layers;
    EffectRun bloom;  // section 5; usable only when built
    std::set<int64_t> referencedComposites;
    std::map<int64_t, size_t> layerById;  // object id to index in layers
    TargetPool pool;
    std::list<Frame> frames;
    Frame* frame = nullptr;  // the one being drawn
    Mat4 viewProj = Mat4::identity();
    GLuint vao = 0, vbo = 0;
    GLuint blitProgram = 0;
    GLint blitMatrix = -1, blitTexture = -1, blitOpaque = -1;
    bool logPasses = false;  // this frame's passes go to the log
    double time = 0, frameTime = 0;
    uint64_t generation = 0;
    float pointer[2] = {0.5f, 0.5f};
    float projection[2] = {1920, 1080};
    std::set<std::string> problemSet;
    std::vector<std::string> problems;

    void problem(const std::string& text) {
        if (problemSet.insert(text).second) {
            log::warn() << "scene: " << text;
            problems.push_back(text);
        }
    }

    // ------------------------------------------------------------- load

    std::shared_ptr<MaterialFile> material(const std::string& path) {
        auto it = scene.materials.find(path);
        if (it != scene.materials.end()) return it->second;
        auto asset = assets.find(path);
        std::shared_ptr<MaterialFile> out;
        if (!asset) {
            problem(path + ": not found");
        } else {
            std::string error;
            auto json = parseJson(asset->bytes, error);
            if (!json) {
                problem(path + ": " + error);
            } else {
                LoadReport report;
                out = std::make_shared<MaterialFile>(parseMaterialFile(*json, report, path));
            }
        }
        scene.materials[path] = out;
        return out;
    }

    void dumpProgram(const TranslatedProgram& t, const ProgramRequest& request,
                     const std::string& where) {
        std::error_code ec;
        std::filesystem::create_directories(options.debug.dumpPrograms, ec);
        const std::string base =
            (options.debug.dumpPrograms / std::to_string(programIndex.size())).string();
        auto write = [](const std::string& path, const std::string& text) {
            if (FILE* f = std::fopen(path.c_str(), "wb")) {
                std::fwrite(text.data(), 1, text.size(), f);
                std::fclose(f);
            }
        };
        write(base + ".vert", t.vertex);
        write(base + ".frag", t.fragment);
        write(base + ".txt", where + "\n" + programKey(request) + "\n" + t.error + "\n");
    }

    Program* program(const ProgramRequest& request, const std::string& where) {
        if (request.shader.empty()) return nullptr;
        std::string key = programKey(request);
        auto it = programIndex.find(key);
        if (it != programIndex.end()) return it->second.get();
        auto p = std::make_unique<Program>();
        p->where = where;
        TranslatedProgram t = translateProgram(loader, request);
        if (!options.debug.dumpPrograms.empty()) dumpProgram(t, request, where);
        if (!t.ok()) {
            problem(where + " (" + request.shader + "): " + t.error);
        } else if (!t.requirements.empty()) {
            std::string list;
            for (const auto& r : t.requirements) list += (list.empty() ? "" : ", ") + r;
            problem(where + " (" + request.shader + "): needs " + list + " (later phase)");
        } else {
            p->handle = programs.request(t.vertex, t.fragment);
            p->requested = true;
            p->interface = std::move(t.interface);
            p->combos = std::move(t.combos);
            auto sprite = p->combos.find("SPRITESHEET");
            p->spriteSheet = sprite != p->combos.end() && sprite->second != 0;
            for (const auto& u : p->interface.uniforms) {
                if (u.type.rfind("sampler", 0) == 0) {
                    auto slot = textureSlot(u.name, "g_Texture", "");
                    if (!slot) continue;
                    p->sampler[size_t(*slot)] = u.type == "sampler3D"     ? GL_TEXTURE_3D
                                                : u.type == "samplerCube" ? GL_TEXTURE_CUBE_MAP
                                                                          : GL_TEXTURE_2D;
                    if (u.annotation.is_object() && u.annotation.contains("default") &&
                        u.annotation["default"].is_string())
                        p->samplerDefault[size_t(*slot)] = u.annotation["default"].get<std::string>();
                } else if (u.annotation.is_object() && u.annotation.contains("material") &&
                           u.annotation["material"].is_string()) {
                    Constant c;
                    c.uniform = u.name;
                    c.material = u.annotation["material"].get<std::string>();
                    c.type = u.type;
                    if (u.annotation.contains("default"))
                        c.fallback = floatsOf(u.annotation["default"]);
                    p->constants.push_back(std::move(c));
                }
            }
        }
        return programIndex.emplace(key, std::move(p)).first->second.get();
    }

    // Links what program() requested and looks the uniforms up.
    void finishPrograms() {
        for (auto& [key, p] : programIndex) {
            if (!p->requested) continue;
            const ProgramCache::Result& r = programs.result(p->handle);
            if (!r.program) {
                std::string first = r.error.substr(0, r.error.find('\n'));
                problem(p->where + ": program does not link: " + first);
                continue;
            }
            p->id = r.program;
            glUseProgram(p->id);
            auto loc = [&](const char* name) { return glGetUniformLocation(p->id, name); };
            EngineLocations& e = p->engine;
            e.mvp = loc("g_ModelViewProjectionMatrix");
            e.mvpInverse = loc("g_ModelViewProjectionMatrixInverse");
            e.effectMvp = loc("g_EffectModelViewProjectionMatrix");
            e.effectMvpInverse = loc("g_EffectModelViewProjectionMatrixInverse");
            e.model = loc("g_ModelMatrix");
            e.modelInverse = loc("g_ModelMatrixInverse");
            e.effectModel = loc("g_EffectModelMatrix");
            e.viewProjection = loc("g_ViewProjectionMatrix");
            e.normalModel = loc("g_NormalModelMatrix");
            e.time = loc("g_Time");
            e.frameTime = loc("g_Frametime");
            e.screen = loc("g_Screen");
            e.texelSize = loc("g_TexelSize");
            e.texelSizeHalf = loc("g_TexelSizeHalf");
            e.pointer = loc("g_PointerPosition");
            e.pointerLast = loc("g_PointerPositionLast");
            e.parallax = loc("g_ParallaxPosition");
            e.color4 = loc("g_Color4");
            e.color = loc("g_Color");
            e.alpha = loc("g_Alpha");
            e.eye = loc("g_EyePosition");
            for (int i = 0; i < kSlots; i++) {
                std::string n = "g_Texture" + std::to_string(i);
                e.resolution[size_t(i)] = loc((n + "Resolution").c_str());
                e.rotation[size_t(i)] = loc((n + "Rotation").c_str());
                e.translation[size_t(i)] = loc((n + "Translation").c_str());
                e.mipInfo[size_t(i)] = loc((n + "MipMapInfo").c_str());
                if (p->sampler[size_t(i)]) {
                    GLint s = loc(n.c_str());
                    if (s >= 0) glUniform1i(s, i);
                    else p->sampler[size_t(i)] = 0;  // optimized out
                }
            }
            for (auto& c : p->constants) c.location = loc(c.uniform.c_str());
            p->usable = true;
        }
        glUseProgram(0);
    }

    Slot classify(const std::string& name, const EffectRun* effect, int64_t self,
                  const std::string& where) {
        Slot s;
        auto composite = compositeObject(name);
        const bool effectBuffer =
            effect && std::any_of(effect->buffers.begin(), effect->buffers.end(),
                                  [&](const EffectBuffer& b) { return b.name == name; });
        if (effectBuffer) {
            // The effect's own fbos, by name; not every name starts _rt_.
            s.kind = SlotKind::Buffer;
            s.name = name;
        } else if (name == "previous" || (composite && *composite == self)) {
            // A layer naming its own composite means what binds call
            // previous (section 3).
            s.kind = SlotKind::Previous;
        } else if (composite) {
            s.kind = SlotKind::Composite;
            s.object = *composite;
        } else if (name == "_rt_FullFrameBuffer") {
            s.kind = SlotKind::Frame;
        } else if (name == "_rt_MipMappedFrameBuffer") {
            s.kind = SlotKind::MipFrame;
        } else if (name.rfind("_rt_", 0) == 0) {
            problem(where + ": unknown render target " + name);
        } else if (name[0] == '$') {
            problem(where + ": system texture " + name + " (later phase)");
        } else {
            s.kind = SlotKind::Texture;
            s.texture = &textures.get(name);
        }
        return s;
    }

    // Texture slots, first found wins: bind, instance, material, sampler
    // default; an effect pass's empty slot 0 is "previous" (section 3).
    void resolveSlots(Pass& pass, const MaterialPass& material,
                      const std::vector<std::optional<std::string>>* instanceTextures,
                      const std::vector<EffectBind>* binds, const EffectRun* effect, int64_t self) {
        if (!pass.program) return;
        for (int i = 0; i < kSlots; i++) {
            if (!pass.program->sampler[size_t(i)]) continue;
            std::string name;
            if (binds)
                for (const auto& b : *binds)
                    if (b.index == i) name = b.name;
            if (name.empty() && instanceTextures && size_t(i) < instanceTextures->size() &&
                (*instanceTextures)[size_t(i)])
                name = *(*instanceTextures)[size_t(i)];
            if (name.empty() && size_t(i) < material.textures.size() && material.textures[size_t(i)])
                name = *material.textures[size_t(i)];
            if (name.empty()) name = pass.program->samplerDefault[size_t(i)];
            if (name.empty()) {
                if (effect && i == 0) pass.slots[0].kind = SlotKind::Previous;
                continue;
            }
            pass.slots[size_t(i)] = classify(name, effect, self, pass.where);
            pass.needsFrame |= pass.slots[size_t(i)].kind == SlotKind::Frame;
            pass.needsMipFrame |= pass.slots[size_t(i)].kind == SlotKind::MipFrame;
        }
    }

    static const Field<Floats>* findConstant(const std::map<std::string, Field<Floats>>& map,
                                             const std::string& name) {
        auto it = map.find(name);
        if (it != map.end()) return &it->second;
        for (const auto& [key, field] : map)
            if (equalsIgnoreCase(key, name)) return &field;
        return nullptr;
    }

    void bindConstants(Pass& pass, const MaterialPass& material,
                       const std::map<std::string, Field<Floats>>* instance, bool objectDefaults) {
        if (!pass.program) return;
        for (const Constant& c : pass.program->constants) {
            ConstantSource s;
            if (instance) s.instance = findConstant(*instance, c.material);
            s.material = findConstant(material.constants, c.material);
            for (const auto& [uniform, property] : material.userShaderValues)
                if (uniform == c.uniform || equalsIgnoreCase(uniform, c.material)) s.property = property;
            if (objectDefaults) {
                s.objectBrightness = c.material == "Brightness";
                s.objectAlpha = c.material == "Alpha";
            }
            pass.sources.push_back(std::move(s));
        }
    }

    Pass materialPass(const MaterialPass& mp, ProgramRequest request, const std::string& where,
                      const std::vector<std::optional<std::string>>* instanceTextures,
                      const std::map<std::string, Field<Floats>>* instanceConstants,
                      const std::vector<EffectBind>* binds, const EffectRun* effect,
                      bool objectDefaults, int64_t self) {
        Pass pass;
        pass.where = where;
        pass.program = program(request, where);
        pass.blend = blendOf(mp.blending);
        pass.alphaWrite = mp.alphaWriting != "disabled";
        resolveSlots(pass, mp, instanceTextures, binds, effect, self);
        bindConstants(pass, mp, instanceConstants, objectDefaults);
        return pass;
    }

    static GLenum bufferFormat(const std::string& format) {
        if (format == "r8") return GL_R8;
        if (format == "rg88") return GL_RG8;
        return GL_RGBA8;  // rgba8888, rgba_backbuffer (HDR comes with bloom)
    }

    EffectRun effectRun(const SceneObject& o, const EffectInstance& instance, size_t index) {
        EffectRun run;
        run.instance = &instance;
        run.where = "object " + std::to_string(o.id) + " effect " + std::to_string(index);
        auto ef = scene.effects.find(instance.file);
        if (ef == scene.effects.end() || !ef->second) {
            problem(run.where + ": " + instance.file + " not loaded");
            run.usable = false;
            return run;
        }
        const EffectFile& file = *ef->second;
        for (const auto& f : file.fbos) {
            EffectBuffer b;
            b.name = f.name;
            b.internal = bufferFormat(f.format);
            b.divisor = std::max(1, int(std::lround(f.scale)));
            b.width = f.width;
            b.height = f.height;
            b.unique = f.unique;
            run.buffers.push_back(std::move(b));
        }
        for (size_t p = 0; p < file.passes.size(); p++) {
            const EffectPassDefinition& d = file.passes[p];
            const EffectPassSettings* settings = p < instance.passes.size() ? &instance.passes[p] : nullptr;
            std::string where = run.where + " pass " + std::to_string(p);
            if (d.command == "copy") {
                Pass pass;
                pass.where = where;
                pass.copy = true;
                pass.source = d.source;
                pass.target = d.target;
                run.passes.push_back(std::move(pass));
                continue;
            }
            if (!d.command.empty()) problem(where + ": command " + d.command + " skipped");
            if (d.material.empty()) continue;
            auto mat = material(d.material);
            if (!mat || mat->passes.empty()) {
                run.usable = false;
                continue;
            }
            if (d.compose) problem(where + ": compose pass drawn like other passes");
            const MaterialPass& mp = mat->passes.front();
            ProgramRequest request = programRequest(mp, scene, settings ? &settings->combos : nullptr,
                                                    settings ? &settings->textures : nullptr);
            Pass pass = materialPass(mp, request, where, settings ? &settings->textures : nullptr,
                                     settings ? &settings->constants : nullptr, &d.bind, &run, false,
                                     o.id);
            pass.target = d.target;
            run.passes.push_back(std::move(pass));
        }
        return run;
    }

    void buildLayer(size_t index) {
        SceneObject& o = scene.scene.objects[index];
        if (!o.image) return;
        const std::string where = "object " + std::to_string(o.id);
        auto model = scene.models.find(o.image->model);
        if (model == scene.models.end() || !model->second) {
            problem(where + ": model " + o.image->model + " not loaded");
            return;
        }
        auto mat = scene.materials.find(model->second->material);
        if (mat == scene.materials.end() || !mat->second || mat->second->passes.empty()) {
            problem(where + ": material " + model->second->material + " not loaded");
            return;
        }
        Layer layer;
        layer.object = index;
        layer.id = o.id;
        layer.referenced = referencedComposites.count(o.id) != 0;
        layer.model = model->second.get();
        const ModelFile& m = *layer.model;
        layer.kind = m.solidLayer    ? LayerKind::Solid
                     : m.fullscreen  ? LayerKind::Fullscreen
                     : m.passthrough ? LayerKind::Compose
                                     : LayerKind::Image;
        const InstanceMaterial* inst = o.image->instance ? &*o.image->instance : nullptr;
        for (size_t p = 0; p < mat->second->passes.size(); p++) {
            const MaterialPass& mp = mat->second->passes[p];
            ProgramRequest request = programRequest(mp, scene, inst ? &inst->combos : nullptr,
                                                    inst ? &inst->textures : nullptr);
            int blendMode = int(o.image->colorBlendMode.value);
            if (blendMode != 0) request.combos["BLENDMODE"] = blendMode;
            layer.passes.push_back(materialPass(mp, request, where + " pass " + std::to_string(p),
                                                inst ? &inst->textures : nullptr, nullptr, nullptr,
                                                nullptr, true, o.id));
        }
        if (layer.passes.front().slots[0].kind == SlotKind::Texture)
            layer.texture = layer.passes.front().slots[0].texture;
        for (size_t e = 0; e < o.effects.size(); e++)
            layer.effects.push_back(effectRun(o, o.effects[e], e));
        if (layer.kind == LayerKind::Image && (!layer.effects.empty() || layer.referenced)) {
            const bool compose = o.image->copyBackground.value;
            auto util = material(compose ? kComposeBackgroundMaterial : kPassthroughMaterial);
            if (util && !util->passes.empty()) {
                const MaterialPass& mp = util->passes.front();
                ProgramRequest request = programRequest(mp, scene, nullptr, nullptr);
                request.textureSet[0] = true;
                if (layer.texture) {
                    request.textureFormat[0] = int(layer.texture->formatCode);
                    if (layer.texture->spriteSheet) request.combos["SPRITESHEET"] = 1;
                }
                layer.base = materialPass(mp, request, where + " base pass", nullptr, nullptr,
                                          nullptr, nullptr, false, o.id);
                layer.base.slots[0].kind = SlotKind::Layer;
                layer.hasBase = true;
            }
        }
        layers.push_back(std::move(layer));
    }

    // The bloom chain of section 5, for scenes whose bloom is on or bound.
    // HDR scenes keep no bloom until the HDR chain is specified.
    void buildBloom() {
        const General& g = scene.scene.general;
        bloom.usable = false;
        if (!(g.bloom.value || g.bloom.bound()) || g.hdr.value) return;
        bloom.usable = true;
        bloom.where = "bloom";
        for (const auto& step : kBloomSteps) {
            if (!*step.target) continue;
            EffectBuffer b;
            b.name = step.target;
            b.divisor = step.divisor;
            bloom.buffers.push_back(std::move(b));
        }
        for (size_t i = 0; i < std::size(kBloomSteps); i++) {
            const BloomStep& step = kBloomSteps[i];
            auto mat = material(step.material);
            if (!mat || mat->passes.empty()) {
                bloom.usable = false;
                return;
            }
            const MaterialPass& mp = mat->passes.front();
            Pass pass = materialPass(mp, programRequest(mp, scene, nullptr, nullptr),
                                     "bloom pass " + std::to_string(i), nullptr, nullptr, nullptr,
                                     &bloom, false, 0);
            pass.target = step.target;
            if (pass.program)
                for (size_t c = 0; c < pass.program->constants.size(); c++) {
                    const std::string& name = pass.program->constants[c].material;
                    if (name == "bloomstrength") pass.sources[c].sceneFloat = &g.bloomStrength;
                    if (name == "bloomthreshold") pass.sources[c].sceneFloat = &g.bloomThreshold;
                    if (name == "bloomtint") pass.sources[c].sceneVec3 = &g.bloomTint;
                }
            bloom.passes.push_back(std::move(pass));
        }
    }

    // After finishPrograms(): drop what cannot draw.
    void settleLayers() {
        for (auto& layer : layers) {
            for (const auto& pass : layer.passes)
                if (!pass.usable()) layer.usable = false;
            for (auto& run : layer.effects)
                for (const auto& pass : run.passes)
                    if (!pass.usable()) run.usable = false;
            if (layer.hasBase && !layer.base.usable()) {
                layer.hasBase = false;
                for (auto& run : layer.effects) run.usable = false;
            }
            if (!layer.usable)
                problem("object " + std::to_string(scene.scene.objects[layer.object].id) +
                        ": not drawn (its material does not compile)");
        }
        std::erase_if(layers, [](const Layer& l) { return !l.usable; });
    }

    // Object ids whose composite another layer samples (section 3).
    void findReferencedComposites() {
        auto note = [&](const std::optional<std::string>& name, int64_t self) {
            if (!name) return;
            if (auto id = compositeObject(*name); id && *id != self) referencedComposites.insert(*id);
        };
        for (const auto& o : scene.scene.objects) {
            for (const auto& e : o.effects)
                for (const auto& p : e.passes)
                    for (const auto& t : p.textures) note(t, o.id);
            if (o.image && o.image->instance)
                for (const auto& t : o.image->instance->textures) note(t, o.id);
        }
    }

    bool load(LoadedScene loaded, std::string& error) {
        scene = std::move(loaded);
        layout = std::make_unique<SceneLayout>(scene.scene);
        const General& g = scene.scene.general;
        findReferencedComposites();
        for (size_t i = 0; i < scene.scene.objects.size(); i++) buildLayer(i);
        buildBloom();
        finishPrograms();
        settleLayers();
        for (const auto& pass : bloom.passes)
            if (!pass.usable()) bloom.usable = false;
        for (size_t i = 0; i < layers.size(); i++) layerById[layers[i].id] = i;
        for (const auto& t : textures.problems()) problem(t);
        projection[0] = g.projectionWidth.value;
        projection[1] = g.projectionHeight.value;
        if (!(projection[0] > 0 && projection[1] > 0)) {
            projection[0] = 1920;
            projection[1] = 1080;
            for (const auto& layer : layers)
                if (layer.texture) {
                    projection[0] = float(layer.texture->imageWidth);
                    projection[1] = float(layer.texture->imageHeight);
                    break;
                }
        }
        // Quad buffer: position at 0, texture coordinate at 1 (the fixed
        // locations of ../shader/README.md section 6); a_Color is white.
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(Quad), nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                              reinterpret_cast<const void*>(3 * sizeof(float)));
        glBindVertexArray(0);
        makeBlitProgram();
        if (layers.empty()) {
            error = "no layer this renderer can draw yet";
            return false;
        }
        return true;
    }

    void makeBlitProgram() {
        GLuint vs = compileStage(GL_VERTEX_SHADER, kBlitVertex);
        GLuint fs = compileStage(GL_FRAGMENT_SHADER, kBlitFragment);
        blitProgram = glCreateProgram();
        glAttachShader(blitProgram, vs);
        glAttachShader(blitProgram, fs);
        glLinkProgram(blitProgram);
        glDeleteShader(vs);
        glDeleteShader(fs);
        GLint ok = 0;
        glGetProgramiv(blitProgram, GL_LINK_STATUS, &ok);
        if (!ok) problem("internal blit program does not link");
        blitMatrix = glGetUniformLocation(blitProgram, "u_Matrix");
        blitTexture = glGetUniformLocation(blitProgram, "u_Texture");
        blitOpaque = glGetUniformLocation(blitProgram, "u_Opaque");
    }

    // ------------------------------------------------------------- draw

    void ensureFrameCopy(bool mip) {
        Frame& f = *frame;
        if (!mip && !f.copyValid) {
            if (!f.copy.fbo) createTarget(f.copy, f.target.width, f.target.height, GL_RGBA8);
            copyTarget(f.target, f.copy);
            f.copyValid = true;
        }
        if (mip && !f.mipValid) {
            if (!f.mipCopy.fbo)
                createTarget(f.mipCopy, f.target.width, f.target.height, GL_RGBA8, true);
            copyTarget(f.target, f.mipCopy);
            glBindTexture(GL_TEXTURE_2D, f.mipCopy.texture);
            glGenerateMipmap(GL_TEXTURE_2D);
            f.mipValid = true;
        }
    }

    void drawQuad(const Quad& quad) {
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(Quad), quad.data());
        glVertexAttrib4f(4, 1, 1, 1, 1);  // a_Color when the shader reads it
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    static void setBlend(Blend blend) {
        switch (blend) {
            case Blend::Normal: glDisable(GL_BLEND); break;
            case Blend::Translucent:
                glEnable(GL_BLEND);
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                                    GL_ONE_MINUS_SRC_ALPHA);
                break;
            case Blend::Additive:
                glEnable(GL_BLEND);
                glBlendFunc(GL_ONE, GL_ONE);
                break;
        }
    }

    static void setMatrix(GLint location, const Mat4& m) {
        if (location >= 0) glUniformMatrix4fv(location, 1, GL_FALSE, m.m.data());
    }

    void uploadConstant(const Constant& c, const Floats& f) {
        std::array<float, 4> v = c.fallback.v;
        if (f.count == 1) v.fill(f.v[0]);
        else
            for (int i = 0; i < f.count; i++) v[size_t(i)] = f.v[size_t(i)];
        const std::string& t = c.type;
        if (t == "float") glUniform1f(c.location, v[0]);
        else if (t == "vec2") glUniform2f(c.location, v[0], v[1]);
        else if (t == "vec3") glUniform3f(c.location, v[0], v[1], v[2]);
        else if (t == "vec4") glUniform4f(c.location, v[0], v[1], v[2], v[3]);
        else if (t == "int" || t == "bool") glUniform1i(c.location, int(std::lround(v[0])));
        else if (t == "uint") glUniform1ui(c.location, unsigned(std::max(0L, std::lround(v[0]))));
    }

    void setConstants(const Pass& pass, const SceneObject* object) {
        const Program& p = *pass.program;
        for (size_t i = 0; i < p.constants.size(); i++) {
            const Constant& c = p.constants[i];
            if (c.location < 0) continue;
            const ConstantSource& s = pass.sources[i];
            Floats f;
            if (!s.property.empty()) {
                auto it = scene.values.find(s.property);
                if (it != scene.values.end()) f.count = textToFloats(it->second, f.v.data(), 4);
            }
            if (!f.count && s.sceneFloat && s.sceneFloat->present) {
                f.v[0] = s.sceneFloat->value;
                f.count = 1;
            }
            if (!f.count && s.sceneVec3 && s.sceneVec3->present) {
                std::copy_n(s.sceneVec3->value.begin(), 3, f.v.begin());
                f.count = 3;
            }
            if (!f.count && s.instance && s.instance->value.count) f = s.instance->value;
            if (!f.count && s.material && s.material->value.count) f = s.material->value;
            if (!f.count && object && object->image && (s.objectBrightness || s.objectAlpha)) {
                f.v[0] = s.objectBrightness ? object->image->brightness.value : object->image->alpha.value;
                f.count = 1;
            }
            if (!f.count) f = c.fallback;
            uploadConstant(c, f);
        }
    }

    // Binds slot i of a pass (section 3 order), or the stand in, and sets
    // its g_Texture<i> uniforms.
    void bindSlot(const Pass& pass, int i, const DrawInputs& in) {
        const Program& p = *pass.program;
        const GLenum target = p.sampler[size_t(i)];
        if (!target) return;
        const Slot& slot = pass.slots[size_t(i)];
        const GpuTexture* texture = nullptr;
        const Target* buffer = nullptr;
        if (i == 0 && in.override0) {
            buffer = in.override0;
        } else {
            switch (slot.kind) {
                case SlotKind::Texture: texture = slot.texture; break;
                case SlotKind::Layer: texture = in.layerTexture; break;
                case SlotKind::Previous: buffer = in.previous; break;
                case SlotKind::Frame: buffer = &frame->copy; break;
                case SlotKind::MipFrame: buffer = &frame->mipCopy; break;
                case SlotKind::Buffer:
                    if (in.named) {
                        auto it = in.named->find(slot.name);
                        if (it != in.named->end()) buffer = it->second;
                    }
                    break;
                case SlotKind::Composite: {
                    auto it = layerById.find(slot.object);
                    if (it != layerById.end()) buffer = layers[it->second].composite;
                    if (!buffer)
                        problem(pass.where + ": layer " + std::to_string(slot.object) +
                                " has no composite yet");
                    break;
                }
                case SlotKind::Empty: break;
            }
        }
        if (!texture && !buffer) texture = &textures.placeholder();
        glActiveTexture(GLenum(GL_TEXTURE0 + i));
        const EngineLocations& e = p.engine;
        if (buffer) {
            glBindTexture(GL_TEXTURE_2D, buffer->texture);
            float w = float(buffer->width), h = float(buffer->height);
            if (e.resolution[size_t(i)] >= 0) glUniform4f(e.resolution[size_t(i)], w, h, w, h);
            if (e.rotation[size_t(i)] >= 0) glUniform4f(e.rotation[size_t(i)], 1, 0, 0, 1);
            if (e.translation[size_t(i)] >= 0) glUniform2f(e.translation[size_t(i)], 0, 0);
            if (e.mipInfo[size_t(i)] >= 0) glUniform1f(e.mipInfo[size_t(i)], float(buffer->levels - 1));
        } else {
            glBindTexture(texture->target == target ? target : texture->target, texture->id);
            if (e.resolution[size_t(i)] >= 0)
                glUniform4f(e.resolution[size_t(i)], float(texture->width), float(texture->height),
                            float(texture->imageWidth), float(texture->imageHeight));
            if (e.rotation[size_t(i)] >= 0)
                glUniform4fv(e.rotation[size_t(i)], 1, texture->frameRotation);
            if (e.translation[size_t(i)] >= 0)
                glUniform2fv(e.translation[size_t(i)], 1, texture->frameTranslation);
            if (e.mipInfo[size_t(i)] >= 0) glUniform1f(e.mipInfo[size_t(i)], 0.0f);
        }
    }

    static const char* slotName(SlotKind kind) {
        switch (kind) {
            case SlotKind::Texture: return "texture";
            case SlotKind::Previous: return "previous";
            case SlotKind::Buffer: return "buffer";
            case SlotKind::Frame: return "frame";
            case SlotKind::MipFrame: return "mipframe";
            case SlotKind::Layer: return "layer";
            case SlotKind::Composite: return "composite";
            case SlotKind::Empty: break;
        }
        return "empty";
    }

    void logPass(const Pass& pass, const DrawInputs& in) {
        const Target& dest = in.destination ? *in.destination : frame->target;
        std::string slots;
        for (int i = 0; i < kSlots; i++)
            if (pass.program->sampler[size_t(i)])
                slots += " " + std::to_string(i) + ":" +
                         (i == 0 && in.override0 ? "result" : slotName(pass.slots[size_t(i)].kind));
        log::info() << "pass " << pass.where << " -> "
                    << (in.destination ? "buffer " : "frame ") << dest.width << "x" << dest.height
                    << " blend " << int(pass.blend) << " slots" << slots;
    }

    void drawPass(const Pass& pass, const DrawInputs& in) {
        const Program& p = *pass.program;
        if (logPasses) logPass(pass, in);
        if (pass.needsFrame) ensureFrameCopy(false);
        if (pass.needsMipFrame) ensureFrameCopy(true);
        const Target& dest = in.destination ? *in.destination : frame->target;
        glBindFramebuffer(GL_FRAMEBUFFER, dest.fbo);
        glViewport(0, 0, dest.width, dest.height);
        glUseProgram(p.id);
        const EngineLocations& e = p.engine;
        setMatrix(e.mvp, in.mvp);
        if (e.mvpInverse >= 0) setMatrix(e.mvpInverse, in.mvp.inverse());
        setMatrix(e.effectMvp, in.effectMvp);
        if (e.effectMvpInverse >= 0) setMatrix(e.effectMvpInverse, in.effectMvp.inverse());
        setMatrix(e.model, in.model);
        if (e.modelInverse >= 0) setMatrix(e.modelInverse, in.model.inverse());
        setMatrix(e.effectModel, in.model);
        setMatrix(e.viewProjection, viewProj);
        if (e.normalModel >= 0) {
            Mat4 n = in.model.inverse();
            float m3[9] = {n.at(0, 0), n.at(0, 1), n.at(0, 2), n.at(1, 0), n.at(1, 1),
                           n.at(1, 2), n.at(2, 0), n.at(2, 1), n.at(2, 2)};  // transposed
            glUniformMatrix3fv(e.normalModel, 1, GL_FALSE, m3);
        }
        const float fw = float(frame->target.width), fh = float(frame->target.height);
        if (e.time >= 0) glUniform1f(e.time, float(time));
        if (e.frameTime >= 0) glUniform1f(e.frameTime, float(frameTime));
        if (e.screen >= 0) glUniform3f(e.screen, fw, fh, fw / fh);
        if (e.texelSize >= 0) glUniform2f(e.texelSize, 1.0f / fw, 1.0f / fh);
        if (e.texelSizeHalf >= 0) glUniform2f(e.texelSizeHalf, 0.5f / fw, 0.5f / fh);
        if (e.pointer >= 0) glUniform2f(e.pointer, pointer[0], pointer[1]);
        if (e.pointerLast >= 0) glUniform2f(e.pointerLast, pointer[0], pointer[1]);
        if (e.parallax >= 0) glUniform2f(e.parallax, 0.5f, 0.5f);
        if (e.eye >= 0) glUniform3f(e.eye, projection[0] / 2, projection[1] / 2, 1000.0f);
        const ImageLayer* image = in.object ? &*in.object->image : nullptr;
        if (e.color4 >= 0) {
            if (in.white || !image) {
                glUniform4f(e.color4, 1, 1, 1, 1);
            } else {
                const Vec3& c = image->color.value;
                float b = image->brightness.value;
                glUniform4f(e.color4, c[0] * b, c[1] * b, c[2] * b, image->alpha.value);
            }
        }
        if (e.color >= 0) {
            Vec3 c = image ? image->color.value : Vec3{1, 1, 1};
            glUniform3f(e.color, c[0], c[1], c[2]);
        }
        if (e.alpha >= 0) glUniform1f(e.alpha, image ? image->alpha.value : 1.0f);
        setConstants(pass, in.object);
        for (int i = 0; i < kSlots; i++) bindSlot(pass, i, in);
        setBlend(pass.blend);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, pass.alphaWrite ? GL_TRUE : GL_FALSE);
        drawQuad(in.quad);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        if (!in.destination) frame->copyValid = frame->mipValid = false;
    }

    void blit(const Target& source, const Target* destination, const Mat4& matrix,
              const Quad& quad, Blend blend) {
        const Target& dest = destination ? *destination : frame->target;
        glBindFramebuffer(GL_FRAMEBUFFER, dest.fbo);
        glViewport(0, 0, dest.width, dest.height);
        glUseProgram(blitProgram);
        setMatrix(blitMatrix, matrix);
        glUniform1i(blitTexture, 0);
        glUniform1f(blitOpaque, 0.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, source.texture);
        setBlend(blend);
        drawQuad(quad);
        if (!destination) frame->copyValid = frame->mipValid = false;
    }

    Vec2 layerSize(const Layer& layer) const {
        const ImageLayer& image = *scene.scene.objects[layer.object].image;
        Vec2 s = image.size.value;
        if (s[0] > 0 && s[1] > 0) return s;
        if (layer.model->width && layer.model->height && *layer.model->width > 0 &&
            *layer.model->height > 0)
            return {*layer.model->width, *layer.model->height};
        if (layer.texture) return {float(layer.texture->imageWidth), float(layer.texture->imageHeight)};
        return {projection[0], projection[1]};
    }

    // Runs one effect from input into output (section 3).
    void runEffect(EffectRun& run, const Target* input, const Target* output, int bw, int bh,
                   DrawInputs in) {
        std::map<std::string, const Target*> named;
        std::vector<const Target*> pooled;
        for (auto& b : run.buffers) {
            int w = b.width ? int(*b.width) : std::max(1, bw / b.divisor);
            int h = b.height ? int(*b.height) : std::max(1, bh / b.divisor);
            if (b.unique) {
                if (b.persistent.width != std::clamp(w, 1, kMaxBufferSize) ||
                    b.persistent.height != std::clamp(h, 1, kMaxBufferSize) || !b.persistent.fbo) {
                    destroyTarget(b.persistent);
                    createTarget(b.persistent, w, h, b.internal);
                    clearTarget(b.persistent);
                }
                named[b.name] = &b.persistent;
            } else {
                const Target* t = pool.acquire(w, h, b.internal, generation);
                clearTarget(*t);
                pooled.push_back(t);
                named[b.name] = t;
            }
        }
        clearTarget(*output);
        auto lookup = [&](const std::string& name) -> const Target* {
            if (name.empty()) return output;
            if (name == "previous") return input;
            auto it = named.find(name);
            return it == named.end() ? nullptr : it->second;
        };
        in.previous = input;
        in.named = &named;
        in.white = true;
        in.override0 = nullptr;
        for (const Pass& pass : run.passes) {
            if (pass.copy) {
                const Target* from = lookup(pass.source);
                const Target* to = lookup(pass.target);
                if (from && to && from != to) copyTarget(*from, *to);
                continue;
            }
            const Target* dest = lookup(pass.target);
            if (!dest) {
                problem(pass.where + ": unknown target " + pass.target);
                continue;
            }
            in.destination = dest;
            drawPass(pass, in);
        }
        for (const Target* t : pooled) pool.release(t);
    }

    // Draws a layer when shown; computes its composite when referenced.
    void drawLayer(Layer& layer, bool shown) {
        const SceneObject& o = scene.scene.objects[layer.object];
        const bool fullscreen = layer.kind == LayerKind::Fullscreen;
        const Vec2 size = layerSize(layer);
        const LayerRect rect = fullscreen ? LayerRect{-1, -1, 1, 1} : layerRect(size, o.image->alignment.value);
        const Mat4 model = fullscreen ? Mat4::identity() : layout->world(layer.object).matrix();
        const Mat4 screenMvp = fullscreen ? Mat4::identity() : viewProj * model;

        const RenderDebug& debug = options.debug;
        std::vector<EffectRun*> active;
        for (size_t e = 0; e < layer.effects.size(); e++) {
            EffectRun& run = layer.effects[e];
            if (!run.usable || !run.instance->visible.value || debug.baseOnly ||
                debug.skipEffects.count({layer.id, -1}) || debug.skipEffects.count({layer.id, int(e)}))
                continue;
            active.push_back(&run);
        }
        if (layer.kind == LayerKind::Image && !layer.hasBase) active.clear();

        DrawInputs in;
        in.object = &o;
        in.model = model;
        in.effectMvp = screenMvp;
        in.layerTexture = layer.texture;

        // Texture region of the layer texture on the plain quad.
        float uMax = 1, vMax = 1;
        if (layer.texture && !layer.passes.front().program->spriteSheet) {
            uMax = layer.texture->uMax;
            vMax = layer.texture->vMax;
        }

        const bool keep = layer.referenced && layer.hasBase;
        if (active.empty() && !keep) {
            if (!shown || layer.kind == LayerKind::Compose || fullscreen) return;  // nothing would change
            in.mvp = screenMvp;
            in.quad = layerQuad(rect, uMax, vMax);
            for (const Pass& pass : layer.passes) drawPass(pass, in);
            return;
        }

        int bw = fullscreen ? frame->target.width : int(std::lround(size[0]));
        int bh = fullscreen ? frame->target.height : int(std::lround(size[1]));
        bw = std::clamp(bw, 1, kMaxBufferSize);
        bh = std::clamp(bh, 1, kMaxBufferSize);
        const Mat4 targetMvp = fullscreen ? Mat4::identity()
                                          : flipY() * Mat4::ortho(rect.x0, rect.x1, rect.y0, rect.y1,
                                                                  -10000, 10000);
        const Quad fullQuad = fullscreen ? clipQuad() : layerQuad(rect, 1, 1);

        const Target* input = pool.acquire(bw, bh, GL_RGBA8, generation);
        clearTarget(*input);
        in.destination = input;
        switch (layer.kind) {
            case LayerKind::Image: {
                bool sprite = layer.base.program->spriteSheet;
                in.mvp = targetMvp;
                in.quad = layerQuad(rect, sprite ? 1 : uMax, sprite ? 1 : vMax);
                in.white = true;
                drawPass(layer.base, in);
                break;
            }
            case LayerKind::Solid:
                in.mvp = targetMvp;
                in.quad = fullQuad;
                for (const Pass& pass : layer.passes) drawPass(pass, in);
                break;
            case LayerKind::Compose:
                // composelayer fills its target from texture coordinates and
                // samples the frame where the layer is on screen.
                in.mvp = screenMvp;
                in.quad = fullQuad;
                for (const Pass& pass : layer.passes) drawPass(pass, in);
                break;
            case LayerKind::Fullscreen:
                in.mvp = Mat4::identity();
                in.quad = fullQuad;
                for (const Pass& pass : layer.passes) drawPass(pass, in);
                break;
        }

        in.mvp = targetMvp;
        in.quad = fullQuad;
        for (EffectRun* run : active) {
            const Target* output = pool.acquire(bw, bh, GL_RGBA8, generation);
            runEffect(*run, input, output, bw, bh, in);
            pool.release(input);
            input = output;
        }

        in.destination = nullptr;
        in.previous = nullptr;
        in.named = nullptr;
        in.white = false;
        if (shown) {
            if (layer.kind == LayerKind::Image) {
                in.mvp = screenMvp;
                in.quad = layerQuad(rect, 1, 1);
                in.override0 = input;
                for (const Pass& pass : layer.passes) drawPass(pass, in);
            } else {
                blit(*input, nullptr, screenMvp, fullQuad, Blend::Translucent);
            }
        }
        if (keep) layer.composite = input;  // released when the frame ends
        else pool.release(input);
    }

    // Runs the bloom chain over the finished frame and writes the result
    // back into it (section 5).
    void applyBloom() {
        const Target& f = frame->target;
        const Target* output = pool.acquire(f.width, f.height, GL_RGBA8, generation);
        DrawInputs in;
        in.mvp = Mat4::identity();
        in.effectMvp = Mat4::identity();
        in.quad = clipQuad();
        runEffect(bloom, &f, output, f.width, f.height, in);
        copyTarget(*output, f);
        frame->copyValid = frame->mipValid = false;
        pool.release(output);
    }

    void drawFrame(Frame& f) {
        frame = &f;
        layout->update();
        const General& g = scene.scene.general;
        const int w = f.target.width, h = f.target.height;
        viewProj = flipY() * viewProjection(projection[0], projection[1], g.zoom.value, w, h,
                                            options.scaling == Scaling::Stretch ? Scaling::Stretch
                                                                                : Scaling::Fill);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_DITHER);
        glBindFramebuffer(GL_FRAMEBUFFER, f.target.fbo);
        glViewport(0, 0, w, h);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        const bool clear = !g.clearEnabled.present || g.clearEnabled.value;
        if (clear || f.generation == ~0ull) {
            const Vec3& c = g.clearColor.value;
            glClearColor(c[0], c[1], c[2], 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        f.copyValid = f.mipValid = false;
        const RenderDebug& debug = options.debug;
        logPasses = debug.passLog && f.generation == ~0ull && frames.size() == 1;
        for (auto& layer : layers) {
            bool shown = layout->visible(layer.object) && !debug.skip.count(layer.id) &&
                         (debug.only.empty() || debug.only.count(layer.id));
            if (shown || layer.referenced) drawLayer(layer, shown);
        }
        for (auto& layer : layers)
            if (layer.composite) {
                pool.release(layer.composite);
                layer.composite = nullptr;
            }
        if (bloom.usable && scene.scene.general.bloom.value && !debug.baseOnly && !debug.noBloom)
            applyBloom();
        logPasses = false;
        f.generation = generation;
        pool.collect(generation);
        glUseProgram(0);
        glBindVertexArray(0);
    }

    // The frame's size for an output and where it lands on it.
    void framePlacement(int width, int height, int& fw, int& fh, int& x, int& y) const {
        fw = width;
        fh = height;
        x = y = 0;
        if (options.scaling != Scaling::Fit) return;
        float s = std::min(float(width) / projection[0], float(height) / projection[1]);
        fw = std::clamp(int(std::lround(projection[0] * s)), 1, width);
        fh = std::clamp(int(std::lround(projection[1] * s)), 1, height);
        x = (width - fw) / 2;
        y = (height - fh) / 2;
    }

    void present(const Frame& f, GLuint destination, int width, int height, int x, int y) {
        glBindFramebuffer(GL_FRAMEBUFFER, destination);
        glViewport(0, 0, width, height);
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glUseProgram(blitProgram);
        setMatrix(blitMatrix, Mat4::identity());
        glUniform1i(blitTexture, 0);
        glUniform1f(blitOpaque, options.debug.keepAlpha ? 0.0f : 1.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, f.target.texture);
        // The output's texture coordinates, so the bars sample outside 0..1
        // and the wrap mode fills them; rows flip back to GL's bottom first.
        float u0 = -float(x) / float(f.target.width);
        float u1 = float(width - x) / float(f.target.width);
        float vTop = -float(y) / float(f.target.height);
        float vBottom = float(height - y) / float(f.target.height);
        GLint wrap = options.clamp == Clamp::Repeat   ? GL_REPEAT
                     : options.clamp == Clamp::Border ? GL_CLAMP_TO_BORDER
                                                      : GL_CLAMP_TO_EDGE;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
        const Vec3& c = scene.scene.general.clearColor.value;
        const float border[4] = {c[0], c[1], c[2], 1.0f};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
        Quad q = {-1, -1, 0, u0, vBottom,  //
                  1,  -1, 0, u1, vBottom,  //
                  -1, 1,  0, u0, vTop,     //
                  1,  1,  0, u1, vTop};
        drawQuad(q);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindVertexArray(0);
        glUseProgram(0);
    }

    void render(int width, int height) {
        if (width <= 0 || height <= 0 || layers.empty()) return;
        GLint bound = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &bound);
        int fw = 0, fh = 0, x = 0, y = 0;
        framePlacement(width, height, fw, fh, x, y);
        Frame* f = nullptr;
        for (auto& candidate : frames)
            if (candidate.target.width == fw && candidate.target.height == fh) f = &candidate;
        if (!f) {
            frames.emplace_back();
            f = &frames.back();
            createTarget(f->target, fw, fh, GL_RGBA8);
        }
        f->lastUse = generation;
        if (f->generation != generation) drawFrame(*f);
        present(*f, GLuint(bound), width, height, x, y);
        // Frames for sizes no output has used for a while go away.
        for (auto it = frames.begin(); it != frames.end();) {
            if (&*it != f && generation - it->lastUse > kUnusedFrames) {
                destroyTarget(it->target);
                destroyTarget(it->copy);
                destroyTarget(it->mipCopy);
                it = frames.erase(it);
            } else {
                ++it;
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, GLuint(bound));
    }
};

// ------------------------------------------------------------ SceneRenderer

SceneRenderer::SceneRenderer(const AssetLocator& assets, Options options)
    : impl_(std::make_unique<Impl>(assets, std::move(options))) {}

SceneRenderer::~SceneRenderer() = default;

bool SceneRenderer::load(LoadedScene scene, std::string& error) {
    return impl_->load(std::move(scene), error);
}

void SceneRenderer::advance(double dt) {
    impl_->time += dt;
    impl_->frameTime = dt;
    impl_->generation++;
}

void SceneRenderer::render(int width, int height) { impl_->render(width, height); }

void SceneRenderer::setProperty(const std::string& name, const std::string& value) {
    applyProperties(impl_->scene, PropertyValues{{name, value}});
    impl_->generation++;  // the next render draws again
}

void SceneRenderer::setPointer(float x, float y) {
    impl_->pointer[0] = std::clamp(x, 0.0f, 1.0f);
    impl_->pointer[1] = std::clamp(y, 0.0f, 1.0f);
}

const std::vector<std::string>& SceneRenderer::problems() const { return impl_->problems; }

size_t SceneRenderer::layerCount() const { return impl_->layers.size(); }

}  // namespace canvas::scene
