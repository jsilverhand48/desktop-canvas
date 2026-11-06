// Implementation of translateProgram declared in Translator.h.
#include "scene/shader/Translator.h"

#include "scene/shader/Conversions.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <set>

namespace canvas::scene {

using nlohmann::json;

namespace {

constexpr const char* kFragOut = "desktopCanvas_FragColor";

bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        lines.emplace_back(text, start, end - start);
        start = end + 1;
    }
    return lines;
}

// The code of a text with comments blanked out (same length, newlines
// kept), so positions found in it are valid in the original.
std::string blankComments(const std::string& text) {
    std::string out = text;
    for (size_t i = 0; i < out.size(); i++) {
        if (out.compare(i, 2, "//") == 0) {
            while (i < out.size() && out[i] != '\n') out[i++] = ' ';
        } else if (out.compare(i, 2, "/*") == 0) {
            size_t end = out.find("*/", i + 2);
            size_t stop = end == std::string::npos ? out.size() : end + 2;
            for (; i < stop; i++)
                if (out[i] != '\n') out[i] = ' ';
            i--;
        }
    }
    return out;
}

struct SamplerCombo {
    int slot = -1;
    std::string combo;
    bool formatCombo = false;
};

// Slot of g_Texture<n>, -1 for other names.
int textureSlot(const std::string& name) {
    static const std::regex re("^g_Texture(\\d+)$");
    std::smatch m;
    if (!std::regex_match(name, m, re)) return -1;
    int slot = std::stoi(m[1]);
    return slot < kMaxTextureSlots ? slot : -1;
}

// [COMBO] lines and sampler annotations of a file and everything it
// includes, regardless of conditions (combos feed the conditions).
void scanAnnotations(const ShaderLoader& loader, const std::string& path, int depth,
                     std::set<std::string>& seen, std::vector<ComboDeclaration>& combos,
                     std::vector<SamplerCombo>& samplers) {
    if (depth > ShaderPreprocessor::kMaxIncludeDepth || !seen.insert(path).second) return;
    auto text = loader(path);
    if (!text) return;
    static const std::regex comboRe(R"(^\s*//\s*\[COMBO\]\s*(\{.*\})\s*$)");
    static const std::regex includeRe(R"(^\s*#\s*include\s+["<]([^">]+)[">])");
    static const std::regex samplerRe(
        R"(^\s*uniform\s+(?:\w+\s+)*?sampler\w*\s+(\w+)\s*;\s*//\s*(\{.*\})\s*$)");
    for (const auto& line : splitLines(normalizeLineEnds(*text))) {
        std::smatch m;
        if (std::regex_match(line, m, comboRe)) {
            json c = json::parse(m[1].str(), nullptr, false);
            if (!c.is_object() || !c.contains("combo") || !c["combo"].is_string()) continue;
            ComboDeclaration d;
            d.name = c["combo"].get<std::string>();
            if (auto def = c.find("default"); def != c.end() && def->is_number())
                d.defaultValue = def->get<int>();
            if (auto type = c.find("type"); type != c.end() && type->is_string())
                d.type = type->get<std::string>();
            bool known = false;
            for (const auto& existing : combos) known |= existing.name == d.name;
            if (!known) combos.push_back(std::move(d));
        } else if (std::regex_search(line, m, includeRe)) {
            std::string file = m[1].str();
            if (file.find("..") == std::string::npos)
                scanAnnotations(loader, "shaders/" + file, depth + 1, seen, combos, samplers);
        } else if (std::regex_match(line, m, samplerRe)) {
            json a = json::parse(m[2].str(), nullptr, false);
            int slot = textureSlot(m[1].str());
            if (!a.is_object() || slot < 0) continue;
            SamplerCombo s;
            s.slot = slot;
            if (auto c = a.find("combo"); c != a.end() && c->is_string()) s.combo = c->get<std::string>();
            if (auto f = a.find("formatcombo"); f != a.end() && f->is_boolean()) s.formatCombo = f->get<bool>();
            if (!s.combo.empty() || s.formatCombo) samplers.push_back(std::move(s));
        }
    }
}

// Declarations of the resolved code: uniforms (with annotations),
// attributes, varyings.
void parseDeclarations(const std::string& code, ShaderInterface& out, bool vertex,
                       std::vector<std::pair<std::string, std::string>>& varyings) {
    static const std::regex uniformRe(
        R"(^\s*uniform\s+((?:(?:highp|mediump|lowp)\s+)?)(\w+)\s+(\w+)\s*(?:\[\s*(\w+)\s*\])?\s*;\s*(?://\s*(\{.*\}))?)");
    static const std::regex ioRe(
        R"(^\s*(attribute|varying)\s+(?:(?:highp|mediump|lowp|flat|smooth|noperspective)\s+)*(\w+)\s+(\w+)\s*;)");
    for (const auto& line : splitLines(code)) {
        std::smatch m;
        if (std::regex_search(line, m, uniformRe)) {
            UniformDeclaration u;
            u.type = m[2].str();
            u.name = m[3].str();
            if (m[4].matched) {
                try {
                    u.arraySize = std::stoi(m[4].str());
                } catch (...) {
                    u.arraySize = -1;  // sized by a macro
                }
            }
            if (m[5].matched) u.annotation = json::parse(m[5].str(), nullptr, false);
            if (u.annotation.is_discarded()) u.annotation = nullptr;
            bool known = false;
            for (const auto& existing : out.uniforms) known |= existing.name == u.name;
            if (!known) out.uniforms.push_back(std::move(u));
        } else if (std::regex_search(line, m, ioRe)) {
            std::pair<std::string, std::string> decl{m[2].str(), m[3].str()};
            if (m[1].str() == "attribute") {
                if (vertex) out.attributes.push_back(decl);
            } else {
                varyings.push_back(decl);
            }
        }
    }
}

// Identifiers used in code (comments blanked).
std::set<std::string> identifiersIn(const std::string& code) {
    std::set<std::string> out;
    std::string c = blankComments(code);
    for (size_t i = 0; i < c.size();) {
        if (identStart(c[i]) && (i == 0 || !identChar(c[i - 1]))) {
            size_t j = i;
            while (j < c.size() && identChar(c[j])) j++;
            out.insert(c.substr(i, j - i));
            i = j;
        } else {
            i++;
        }
    }
    return out;
}

// Position just after the opening brace of main, or npos.
size_t mainBody(const std::string& code) {
    std::string c = blankComments(code);
    static const std::regex mainRe(R"(\bvoid\s+main\s*\(\s*(void)?\s*\)\s*\{)");
    std::smatch m;
    if (!std::regex_search(c, m, mainRe)) return std::string::npos;
    return size_t(m.position(0) + m.length(0));
}

// End of the bracketed expression starting at open ('['), or npos.
size_t matchBracket(const std::string& c, size_t open) {
    int level = 0;
    for (size_t i = open; i < c.size(); i++) {
        if (c[i] == '[') level++;
        else if (c[i] == ']' && --level == 0) return i;
    }
    return std::string::npos;
}

std::string squash(const std::string& s) {
    std::string out;
    for (char ch : s)
        if (!std::isspace(static_cast<unsigned char>(ch))) out.push_back(ch);
    return out;
}

// [req A2 (2)]: name[a][b] on a float array uniform.
std::string repackFloatArrays(const std::string& code, const std::set<std::string>& arrays,
                              std::vector<std::string>& notes) {
    if (arrays.empty()) return code;
    std::string out;
    std::string c = blankComments(code);
    size_t copied = 0;
    size_t count = 0;
    for (size_t i = 0; i < c.size(); i++) {
        if (!identStart(c[i]) || (i > 0 && identChar(c[i - 1]))) continue;
        size_t j = i;
        while (j < c.size() && identChar(c[j])) j++;
        std::string name = c.substr(i, j - i);
        size_t k = j;
        while (k < c.size() && std::isspace(static_cast<unsigned char>(c[k]))) k++;
        if (!arrays.count(name) || k >= c.size() || c[k] != '[') {
            i = j - 1;
            continue;
        }
        size_t close1 = matchBracket(c, k);
        if (close1 == std::string::npos) break;
        size_t l = close1 + 1;
        while (l < c.size() && std::isspace(static_cast<unsigned char>(c[l]))) l++;
        if (l >= c.size() || c[l] != '[') {
            i = close1;
            continue;
        }
        size_t close2 = matchBracket(c, l);
        if (close2 == std::string::npos) break;
        std::string a = code.substr(k + 1, close1 - k - 1);
        std::string b = code.substr(l + 1, close2 - l - 1);
        std::string sa = squash(a), sb = squash(b);
        std::string index;
        std::smatch m;
        static const std::regex divRe(R"(^\(?(\w+)\)?/4$)");
        if (std::regex_match(sa, m, divRe) && sb == m[1].str() + "%4") index = m[1].str();
        else index = "4*(" + a + ") + (" + b + ")";
        out.append(code, copied, k - copied);
        out.append("[" + index + "]");
        copied = close2 + 1;
        i = close2;
        count++;
    }
    out.append(code, copied, std::string::npos);
    if (count) notes.push_back("packed float array indexing rewritten (" + std::to_string(count) + ")");
    return out;
}

}  // namespace

std::string replaceIdentifiers(const std::string& code,
                               const std::map<std::string, std::string>& replacements) {
    std::string c = blankComments(code);
    std::string out;
    out.reserve(code.size());
    size_t i = 0;
    while (i < code.size()) {
        if (identStart(c[i]) && (i == 0 || !identChar(c[i - 1]))) {
            size_t j = i;
            while (j < c.size() && identChar(c[j])) j++;
            auto it = replacements.find(c.substr(i, j - i));
            if (it != replacements.end()) out.append(it->second);
            else out.append(code, i, j - i);
            i = j;
        } else {
            out.push_back(code[i]);
            i++;
        }
    }
    return out;
}

int attributeLocation(const std::string& name) {
    static const std::map<std::string, int> fixed = {
        {"a_Position", 0},       {"a_TexCoord", 1},     {"a_Normal", 2},
        {"a_Tangent4", 3},       {"a_Color", 4},        {"a_PositionVec4", 5},
        {"a_TexCoordVec3", 6},   {"a_TexCoordVec4", 7},
    };
    if (auto it = fixed.find(name); it != fixed.end()) return it->second;
    // a_TexCoordC2, a_TexCoordC1Vec4 and a_TexCoordVec4C1 (genericparticle).
    static const std::regex extra(R"(^a_TexCoord(?:Vec[34])?C([1-5])(?:Vec[34])?$)");
    std::smatch m;
    if (std::regex_match(name, m, extra)) return 7 + std::stoi(m[1].str());
    return -1;
}

const std::string& shaderPrelude() {
    static const std::string prelude =
        "#define texSample2D texture\n"
        "#define texSample2DLod textureLod\n"
        "#define mul(x, y) ((y) * (x))\n"
        "#define frac fract\n"
        "#define atan2 atan\n"
        "#define lerp mix\n"
        "#define fmod(x, y) ((x) - (y) * trunc((x) / (y)))\n"
        "#define ddx dFdx\n"
        "#define ddy dFdy\n"
        "#define saturate(x) clamp(x, 0.0, 1.0)\n"
        "#define CAST2(x) vec2(x)\n"
        "#define CAST3(x) vec3(x)\n"
        "#define CAST4(x) vec4(x)\n"
        "#define CAST3X3(x) mat3(x)\n"
        "#define CASTU(x) uint(x)\n"
        "#define CASTF(x) float(x)\n"
        "#define log10(x) (log2(x) * 0.30102999566398120)\n";
    return prelude;
}

// LightingV1 (src/scene/render/README.md section 12): this project's
// PerformLighting_V1 over the per kind light arrays whose sizes are the
// LIGHTS_* combos, with its own Cook-Torrance term (GGX distribution,
// Schlick-GGX geometry, Schlick Fresnel) and the radius squared over
// distance squared falloff of the asset shaders. Arrays the shader
// declares itself are not declared again.
static std::string lightingCode(const std::map<std::string, int>& combos,
                                const std::vector<UniformDeclaration>& uniforms) {
    auto count = [&](const char* name) {
        auto it = combos.find(name);
        return it == combos.end() ? 0 : std::clamp(it->second, 0, 16);
    };
    std::set<std::string> declared;
    for (const auto& u : uniforms) declared.insert(u.name);
    std::string s = "// desktop-canvas: PerformLighting_V1\n";
    auto array = [&](const char* name, int n) {
        if (n > 0 && !declared.count(name)) s += std::string("uniform vec4 ") + name + "[" + std::to_string(n) + "];\n";
    };
    const int points = count("LIGHTS_POINT"), spots = count("LIGHTS_SPOT"), tubes = count("LIGHTS_TUBE"),
              directionals = count("LIGHTS_DIRECTIONAL");
    array("g_LPoint_Color", points);
    array("g_LPoint_Origin", points);
    array("g_LSpot_Color", spots);
    array("g_LSpot_Origin", spots);
    array("g_LSpot_Direction", spots);
    array("g_LTube_Color", tubes);
    array("g_LTube_OriginA", tubes);
    array("g_LTube_OriginB", tubes);
    array("g_LDirectional_Color", directionals);
    array("g_LDirectional_Direction", directionals);
    s += R"(vec3 desktopCanvas_lightTerm(vec3 n, vec3 toLight, vec3 v, vec3 albedo, vec3 radiance,
                                vec3 tint, vec3 f0, float roughness, float metallic) {
    float d2 = max(dot(toLight, toLight), 0.0001);
    vec3 l = toLight * inversesqrt(d2);
    vec3 h = normalize(v + l);
    float nl = max(dot(n, l), 0.0);
    float nv = max(dot(n, v), 0.0001);
    float nh = max(dot(n, h), 0.0);
    float a = max(roughness * roughness, 0.001);
    float a2 = a * a;
    float q = nh * nh * (a2 - 1.0) + 1.0;
    float distribution = a2 / (3.14159265 * q * q);
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float geometry = (nv / (nv * (1.0 - k) + k)) * (nl / (nl * (1.0 - k) + k));
    vec3 fresnel = f0 + (vec3(1.0) - f0) * pow(1.0 - max(dot(h, v), 0.0), 5.0);
    vec3 specular = tint * (distribution * geometry) * fresnel / max(4.0 * nv * nl, 0.001);
    vec3 diffuse = (vec3(1.0) - fresnel) * (1.0 - metallic) * albedo / 3.14159265;
    return (diffuse + specular) * radiance * (nl / d2);
}
vec3 desktopCanvas_segmentDelta(vec3 p, vec3 a, vec3 b) {
    vec3 ab = b - a;
    float t = clamp(dot(p - a, ab) / max(dot(ab, ab), 0.0001), 0.0, 1.0);
    return a + ab * t - p;
}
vec3 PerformLighting_V1(vec3 worldPos, vec3 albedo, vec3 normal, vec3 viewVector, vec3 specularTint,
                        vec3 f0, float roughness, float metallic) {
    vec3 light = vec3(0.0);
)";
    if (points)
        s += "    for (int i = 0; i < " + std::to_string(points) + R"(; i++)
        light += desktopCanvas_lightTerm(normal, g_LPoint_Origin[i].xyz - worldPos, viewVector, albedo,
            g_LPoint_Color[i].rgb * (g_LPoint_Color[i].w * g_LPoint_Color[i].w), specularTint, f0, roughness, metallic);
)";
    if (spots)
        s += "    for (int i = 0; i < " + std::to_string(spots) + R"(; i++) {
        vec3 toLight = g_LSpot_Origin[i].xyz - worldPos;
        float cone = smoothstep(g_LSpot_Direction[i].w, g_LSpot_Origin[i].w,
                                -dot(normalize(toLight), g_LSpot_Direction[i].xyz));
        light += desktopCanvas_lightTerm(normal, toLight, viewVector, albedo,
            g_LSpot_Color[i].rgb * (cone * g_LSpot_Color[i].w * g_LSpot_Color[i].w), specularTint, f0, roughness, metallic);
    }
)";
    if (tubes)
        s += "    for (int i = 0; i < " + std::to_string(tubes) + R"(; i++)
        light += desktopCanvas_lightTerm(normal,
            desktopCanvas_segmentDelta(worldPos, g_LTube_OriginA[i].xyz, g_LTube_OriginB[i].xyz), viewVector,
            albedo, g_LTube_Color[i].rgb * (g_LTube_Color[i].w * g_LTube_Color[i].w), specularTint, f0, roughness, metallic);
)";
    if (directionals)
        s += "    for (int i = 0; i < " + std::to_string(directionals) + R"(; i++)
        light += desktopCanvas_lightTerm(normal, g_LDirectional_Direction[i].xyz, viewVector, albedo,
            g_LDirectional_Color[i].rgb * (g_LDirectional_Color[i].w * g_LDirectional_Color[i].w), specularTint, f0,
            roughness, metallic);
)";
    s += "    return light;\n}\n";
    return s;
}

TranslatedProgram translateProgram(const ShaderLoader& loader, const ProgramRequest& request) {
    TranslatedProgram out;
    const std::string vertPath = "shaders/" + request.shader + ".vert";
    const std::string fragPath = "shaders/" + request.shader + ".frag";

    // 1. Combos.
    std::vector<SamplerCombo> samplers;
    std::set<std::string> seenVert, seenFrag;
    scanAnnotations(loader, vertPath, 0, seenVert, out.declaredCombos, samplers);
    scanAnnotations(loader, fragPath, 0, seenFrag, out.declaredCombos, samplers);
    for (const auto& c : out.declaredCombos) out.combos[c.name] = c.defaultValue;
    for (const auto& [name, value] : request.combos) out.combos[name] = value;
    for (const auto& s : samplers) {
        if (!s.combo.empty()) out.combos[s.combo] = request.textureSet[size_t(s.slot)] ? 1 : 0;
        if (s.formatCombo)
            out.combos["TEX" + std::to_string(s.slot) + "FORMAT"] =
                std::max(0, request.textureFormat[size_t(s.slot)]);
    }

    // 2. Preprocessing.
    std::map<std::string, std::string> defines = {{"GLSL", "1"}};
    for (const auto& [name, value] : out.combos) defines[name] = std::to_string(value);
    PreprocessResult vert = ShaderPreprocessor(loader, defines).run(vertPath);
    PreprocessResult frag = ShaderPreprocessor(loader, defines).run(fragPath);
    if (!vert.ok()) out.error = "vertex: " + vert.error;
    else if (!frag.ok()) out.error = "fragment: " + frag.error;
    if (!out.ok()) return out;
    bool vertLighting = false, fragLighting = false;
    for (auto* r : {&vert, &frag}) {
        out.notes.insert(out.notes.end(), r->notes.begin(), r->notes.end());
        out.includes.insert(out.includes.end(), r->includes.begin(), r->includes.end());
        for (const auto& name : r->requirements) {
            // #require makes engine code available; only a program that
            // calls into it needs it. LightingV1's entry point is the one
            // function shaders call [corpus]; this project supplies it
            // (section 5). Unknown names always count.
            if (name == "LightingV1") {
                if (identifiersIn(r->text).count("PerformLighting_V1"))
                    (r == &vert ? vertLighting : fragLighting) = true;
                continue;
            }
            if (std::find(out.requirements.begin(), out.requirements.end(), name) ==
                out.requirements.end())
                out.requirements.push_back(name);
        }
    }

    // 3. Interface.
    std::vector<std::pair<std::string, std::string>> fragVaryings;
    parseDeclarations(vert.text, out.interface, true, out.interface.varyings);
    // A uniform both stages declare for different material values gets a
    // vertex copy of its own: Direct3D gives each stage its own constants
    // (section 5) [corpus: foliagesway's g_Strength is "strength" in the
    // vertex stage and "strengthuv" in the fragment stage].
    std::map<std::string, std::string> vertexRenames;
    {
        ShaderInterface fragOnly;
        std::vector<std::pair<std::string, std::string>> ignored;
        parseDeclarations(frag.text, fragOnly, false, ignored);
        auto material = [](const UniformDeclaration& u) {
            return u.annotation.is_object() && u.annotation.contains("material") && u.annotation["material"].is_string()
                       ? u.annotation["material"].get<std::string>()
                       : std::string();
        };
        for (const auto& f : fragOnly.uniforms) {
            if (f.type.rfind("sampler", 0) == 0) continue;
            for (auto& v : out.interface.uniforms) {
                if (v.name != f.name) continue;
                const std::string vm = material(v), fm = material(f);
                if (vm.empty() || fm.empty() || vm == fm) break;
                const std::string renamed = "desktopCanvas_vs_" + v.name;
                vertexRenames[v.name] = renamed;
                v.name = renamed;
                out.notes.push_back("uniform " + f.name + " differs per stage: vertex copy " + renamed);
                break;
            }
        }
    }
    parseDeclarations(frag.text, out.interface, false, fragVaryings);

    // 4. Rewrites and repairs.
    std::string vcode = replaceIdentifiers(vert.text, {{"attribute", "in"}, {"varying", "out"}});
    if (!vertexRenames.empty()) vcode = replaceIdentifiers(vcode, vertexRenames);
    {  // fixed locations for the documented attributes
        static const std::regex inputRe(
            R"(^(\s*)(in\s+(?:(?:highp|mediump|lowp)\s+)?\w+\s+(\w+)\s*;.*)$)");
        std::string located;
        for (const auto& line : splitLines(vcode)) {
            std::smatch m;
            int location = std::regex_match(line, m, inputRe) ? attributeLocation(m[3].str()) : -1;
            if (location >= 0)
                located += m[1].str() + "layout(location = " + std::to_string(location) + ") " +
                           m[2].str() + "\n";
            else
                located += line + "\n";
        }
        vcode = std::move(located);
    }
    std::string fcode =
        replaceIdentifiers(frag.text, {{"varying", "in"}, {"gl_FragColor", kFragOut}});
    std::string fragDeclarations = std::string("out vec4 ") + kFragOut + ";\n";

    // [req A2 (1)] varyings the fragment shader uses but only the vertex
    // shader declares.
    std::set<std::string> fragNames;
    for (const auto& v : fragVaryings) fragNames.insert(v.second);
    std::set<std::string> fragUses = identifiersIn(fcode);
    for (const auto& [type, name] : out.interface.varyings) {
        if (fragNames.count(name) || !fragUses.count(name)) continue;
        fragDeclarations += "in " + type + " " + name + ";\n";
        fragVaryings.emplace_back(type, name);
        fragNames.insert(name);
        out.notes.push_back("declared varying " + name + " in the fragment shader");
    }

    // [req A2 (2)] packed float array indexing, both stages.
    std::set<std::string> floatArrays;
    for (const auto& u : out.interface.uniforms)
        if (u.type == "float" && u.arraySize != 0) floatArrays.insert(u.name);
    vcode = repackFloatArrays(vcode, floatArrays, out.notes);
    fcode = repackFloatArrays(fcode, floatArrays, out.notes);

    // [req A2 (3)] fragment writes to varyings go to a global copy.
    std::string mainInit;
    for (const auto& [type, name] : fragVaryings) {
        std::string c = blankComments(fcode);
        std::regex write("\\b" + name +
                         R"(\b\s*(\.\s*[xyzwrgbastpq]+\s*)?(=(?!=)|\+=|-=|\*=|/=))");
        if (!std::regex_search(c, write)) continue;
        std::string copy = "desktopCanvas_v_" + name;
        fcode = replaceIdentifiers(fcode, {{name, copy}});
        // Restore the input declaration (if the code has one) and declare
        // the copy right after it.
        std::regex decl("(\\bin\\s+[^;]*?\\b)" + copy + "(\\s*;)");
        if (std::regex_search(fcode, decl)) {
            fcode = std::regex_replace(fcode, decl, "$1" + name + "$2\n" + type + " " + copy + ";",
                                       std::regex_constants::format_first_only);
        } else {
            fragDeclarations += type + " " + copy + ";\n";
        }
        mainInit += copy + " = " + name + ";\n";
        out.notes.push_back("fragment shader writes varying " + name + ": using a copy");
    }
    if (!mainInit.empty()) {
        size_t body = mainBody(fcode);
        if (body == std::string::npos) {
            out.error = "fragment: no main() to initialise varying copies in";
            return out;
        }
        fcode.insert(body, "\n" + mainInit);
    }

    // Uniform declarations above the code (blank lines keep the numbering).
    auto hoist = [](std::string& code, const PreprocessResult& r) {
        static const std::regex uniformLine(
            R"(^\s*uniform\s+(?:(?:highp|mediump|lowp)\s+)?(?:float|int|uint|bool|[iu]?vec[234]|mat[234](?:x[234])?|sampler\w+)\s+\w+\s*(?:\[\s*(\w+)\s*\])?\s*;\s*(?://.*)?$)");
        std::string hoisted, body;
        for (const auto& line : splitLines(code)) {
            std::smatch m;
            if (std::regex_match(line, m, uniformLine)) {
                std::string decl = line;
                if (m[1].matched) {  // an array sized by a macro defined in the code
                    auto macro = r.macros.find(m[1].str());
                    if (macro != r.macros.end())
                        decl = std::regex_replace(decl, std::regex("\\[\\s*" + m[1].str() + "\\s*\\]"),
                                                  "[" + macro->second + "]");
                }
                hoisted += decl + "\n";
                body += "\n";
            } else {
                body += line + "\n";
            }
        }
        code = std::move(body);
        return hoisted;
    };
    std::string vertUniforms = hoist(vcode, vert);
    std::string fragUniforms = hoist(fcode, frag);
    if (vertLighting) {
        vertUniforms += lightingCode(out.combos, out.interface.uniforms);
        out.notes.push_back("supplied PerformLighting_V1 (vertex)");
    }
    if (fragLighting) {
        fragUniforms += lightingCode(out.combos, out.interface.uniforms);
        out.notes.push_back("supplied PerformLighting_V1");
    }

    // 5. Assembly.
    auto assemble = [&](const PreprocessResult& r, const std::string& code,
                        const std::string& declarations) {
        std::set<std::string> ownDefines;
        static const std::regex defineRe(R"(^\s*#\s*define\s+(\w+))");
        for (const auto& line : splitLines(code)) {
            std::smatch m;
            if (std::regex_search(line, m, defineRe)) ownDefines.insert(m[1].str());
        }
        std::string s = "#version 330 core\n";
        for (const auto& e : r.extensions) s += e + "\n";
        s += "#define GLSL 1\n";
        for (const auto& [name, value] : out.combos)
            if (!ownDefines.count(name)) s += "#define " + name + " " + std::to_string(value) + "\n";
        s += shaderPrelude();
        s += declarations;
        s += "#line 1\n";
        s += code;
        return s;
    };
    out.vertex = assemble(vert, vcode, vertUniforms);
    out.fragment = assemble(frag, fcode, fragDeclarations + fragUniforms);

    // 6. HLSL conversions (section 6.1). Fragment inputs the vertex stage
    // outputs wider take the vertex type. A stage that does not parse keeps
    // its text.
    std::map<std::string, GlslType> widenedInputs, widenedOutputs;
    for (const auto& [ftype, name] : fragVaryings)
        for (const auto& [vtype, vname] : out.interface.varyings) {
            if (vname != name) continue;
            GlslType f = builtinType(ftype), v = builtinType(vtype);
            if (!f.isScalarOrVector() || !v.isScalarOrVector() || f.base != v.base) continue;
            if (v.size > f.size) widenedInputs[name] = v;
            if (f.size > v.size) widenedOutputs[name] = f;
        }
    auto convertStage = [&](std::string& program, const std::map<std::string, GlslType>& inputs,
                            const std::map<std::string, GlslType>& outputs, const char* stage) {
        std::string error;
        auto converted = applyHlslConversions(program, inputs, outputs, error);
        if (!converted) {
            out.notes.push_back(std::string(stage) + ": HLSL conversion pass skipped: " + error);
            return;
        }
        program = std::move(converted->program);
        for (const auto& n : converted->notes) out.notes.push_back(std::string(stage) + ": " + n);
    };
    convertStage(out.vertex, {}, widenedOutputs, "vertex");
    convertStage(out.fragment, widenedInputs, {}, "fragment");
    return out;
}

}  // namespace canvas::scene
