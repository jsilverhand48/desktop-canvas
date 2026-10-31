// Program translation, per src/scene/shader/README.md sections 3 to 6.
//
// translateProgram() turns one material pass's shader (shaders/<name>.vert
// and .frag) into GLSL 3.30 core sources:
// 1. combos: the [COMBO] defaults of both stages and their includes, then
//    the request's material and instance values, then the texture driven
//    defines (a sampler's "combo" is 1 when its slot is set, "formatcombo"
//    gives TEX<n>FORMAT the slot's format code);
// 2. each stage goes through ShaderPreprocessor with GLSL=1 and the combos;
// 3. the resolved code is rewritten (attribute/varying to in/out,
//    gl_FragColor to a declared output) and repaired [req A2 (1) to (3)];
//    uniform declarations of built in types move above the code, because
//    Wallpaper Engine's own headers use uniforms their shaders declare
//    after the #include (section 6);
// 4. the result is #version 330 core, the #extension lines, the combo
//    defines, the prelude, the repairs' declarations, then the code;
// 5. each stage then goes through the HLSL conversion pass (Conversions.h,
//    section 6.1), with varyings of different widths in the two stages
//    reconciled; a stage that does not parse keeps the step 4 text (a
//    note says why). The final sources are therefore macro expanded.
// The interface (uniforms with their annotations, attributes, varyings)
// comes from the resolved code, so it lists only what the combos keep.
// Nothing here touches GL; GL compilation is the caller's.
#pragma once

#include <array>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "scene/shader/Preprocessor.h"

namespace canvas::scene {

inline constexpr int kMaxTextureSlots = 16;

struct ComboDeclaration {
    std::string name;
    int defaultValue = 0;
    std::string type;  // options, imageblending, audioprocessingoptions or empty
};

struct UniformDeclaration {
    std::string type;  // as written: float, vec2, sampler2D, ...
    std::string name;
    int arraySize = 0;  // 0 for a plain uniform
    nlohmann::json annotation;  // null without a JSON comment
};

struct ShaderInterface {
    std::vector<UniformDeclaration> uniforms;  // both stages, first declaration wins
    std::vector<std::pair<std::string, std::string>> attributes;  // (type, name)
    std::vector<std::pair<std::string, std::string>> varyings;    // vertex outputs (type, name)
};

struct ProgramRequest {
    std::string shader;                 // e.g. "genericimage2"
    std::map<std::string, int> combos;  // material, then instance values (caller merges)
    std::array<bool, kMaxTextureSlots> textureSet{};  // slot set by material or instance
    std::array<int, kMaxTextureSlots> textureFormat;  // .tex format code, -1 unknown

    ProgramRequest() { textureFormat.fill(-1); }
};

struct TranslatedProgram {
    std::string vertex;
    std::string fragment;
    std::vector<ComboDeclaration> declaredCombos;
    std::map<std::string, int> combos;  // the values used
    ShaderInterface interface;
    std::vector<std::string> includes;
    std::vector<std::string> notes;  // repairs and dropped directives
    // Engine code the shader asks for with #require and actually calls
    // (LightingV1: PerformLighting_V1, phase P8); such a program cannot
    // render until that code exists.
    std::vector<std::string> requirements;
    std::string error;               // empty on success
    bool ok() const { return error.empty(); }
};

TranslatedProgram translateProgram(const ShaderLoader& loader, const ProgramRequest& request);

// The dialect macros of section 1, as emitted.
const std::string& shaderPrelude();

// Fixed vertex attribute locations (section 6): a_Position 0, a_TexCoord
// 1, a_Normal 2, a_Tangent4 3, a_Color 4, a_PositionVec4 5,
// a_TexCoordVec3 6, a_TexCoordVec4 7, a_TexCoordC1 to C5 8 to 12 (their
// Vec3 and Vec4 forms share the index); -1 for other names.
int attributeLocation(const std::string& name);

// Whole identifier replacement outside comments (exposed for tests).
std::string replaceIdentifiers(const std::string& code,
                               const std::map<std::string, std::string>& replacements);

}  // namespace canvas::scene
