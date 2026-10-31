// The conditional preprocessor of src/scene/shader/README.md section 4,
// with the include handling of section 2.
//
// run() expands one shader file. In active code, `#include "file"` pulls
// in shaders/<file> through the loader, once per run (later includes of it
// expand to nothing), at most kMaxIncludeDepth deep. #if, #ifdef,
// #ifndef, #elif, #else and #endif are resolved with C rules: integer
// arithmetic, comparisons, logic, defined(), and names that are not
// defined count as 0. Object like #define and #undef lines are tracked for
// the expressions and passed through (their final values are reported);
// #require names are collected and dropped, as are #version and unknown
// directives; #extension lines are collected for the top of the program.
// A stray #endif becomes a fixed comment line (never the original text)
// and blocks still open at the end are closed [req A2 (4)]; both are
// noted. Line ends are normalized to LF and backslash line
// continuations are joined. Directives inside block comments are text.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace canvas::scene {

// Returns the text of an asset path such as "shaders/common.h", or nullopt.
using ShaderLoader = std::function<std::optional<std::string>(const std::string& path)>;

struct PreprocessResult {
    std::string text;                     // resolved source, LF line ends
    std::vector<std::string> extensions;  // "#extension ..." lines
    std::vector<std::string> includes;    // asset paths pulled in, in order
    std::vector<std::string> notes;       // repairs and dropped directives
    std::vector<std::string> requirements;    // #require names (engine code the shader needs)
    std::map<std::string, std::string> macros;  // object like macros at the end
    std::string error;                    // empty on success
    bool ok() const { return error.empty(); }
};

class ShaderPreprocessor {
public:
    static constexpr int kMaxIncludeDepth = 16;

    // defines: name to value text, visible to expressions and code (the
    // caller also emits them for the driver).
    ShaderPreprocessor(ShaderLoader loader, std::map<std::string, std::string> defines);

    // path: the asset path of the shader, e.g. "shaders/genericimage2.frag".
    PreprocessResult run(const std::string& path);

    // One #if expression against the current macros; nullopt when it does
    // not parse.
    std::optional<int64_t> evaluate(std::string_view expression) const;

private:
    struct Macro {
        bool function = false;
        std::string value;
    };
    struct Conditional {
        bool parentActive = true;
        bool taken = false;  // a branch of this block was active
        bool active = true;
        bool sawElse = false;
    };

    bool active() const { return conditions_.empty() || conditions_.back().active; }
    void processFile(const std::string& path, const std::string& text, int depth);
    void directive(const std::string& line, std::string_view name, std::string_view rest,
                   int depth);

    ShaderLoader loader_;
    std::map<std::string, Macro> macros_;
    std::set<std::string> included_;
    std::vector<Conditional> conditions_;
    PreprocessResult result_;
};

// Every line break (CRLF, lone CR, LF) as LF.
std::string normalizeLineEnds(std::string_view text);

}  // namespace canvas::scene
