// Implementation of ShaderPreprocessor declared in Preprocessor.h.
#include "scene/shader/Preprocessor.h"

#include <cctype>
#include <cstring>

namespace canvas::scene {

std::string normalizeLineEnds(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        if (c == '\r') {
            out.push_back('\n');
            if (i + 1 < text.size() && text[i + 1] == '\n') i++;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

namespace {

bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

std::string_view trimView(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

// The text of a directive's argument without comments.
std::string stripComments(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s.compare(i, 2, "//") == 0) break;
        if (s.compare(i, 2, "/*") == 0) {
            size_t end = s.find("*/", i + 2);
            if (end == std::string_view::npos) break;
            i = end + 1;
            out.push_back(' ');
            continue;
        }
        out.push_back(s[i]);
    }
    return out;
}

// Whether a block comment is still open after this line, given its state
// at the start.
bool blockCommentAfter(std::string_view line, bool inBlock) {
    for (size_t i = 0; i < line.size(); i++) {
        if (inBlock) {
            if (line.compare(i, 2, "*/") == 0) {
                inBlock = false;
                i++;
            }
        } else if (line.compare(i, 2, "//") == 0) {
            break;
        } else if (line.compare(i, 2, "/*") == 0) {
            inBlock = true;
            i++;
        }
    }
    return inBlock;
}

// Recursive descent evaluation of C preprocessor expressions.
class Expression {
public:
    using Lookup = std::function<std::optional<std::string>(const std::string&, bool& function)>;

    Expression(std::string_view text, const Lookup& lookup, int depth)
        : text_(text), lookup_(lookup), depth_(depth) {}

    std::optional<int64_t> run() {
        int64_t v = ternary();
        skip();
        if (failed_ || pos_ != text_.size()) return std::nullopt;
        return v;
    }

private:
    void skip() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) pos_++;
    }
    bool peek(std::string_view op) {
        skip();
        return text_.compare(pos_, op.size(), op) == 0;
    }
    bool accept(std::string_view op) {
        if (!peek(op)) return false;
        pos_ += op.size();
        return true;
    }
    int64_t fail() {
        failed_ = true;
        return 0;
    }

    int64_t ternary() {
        int64_t c = logicalOr();
        if (accept("?")) {
            int64_t a = ternary();
            if (!accept(":")) return fail();
            int64_t b = ternary();
            return c ? a : b;
        }
        return c;
    }
    int64_t logicalOr() {
        int64_t v = logicalAnd();
        while (!failed_ && accept("||")) {
            int64_t r = logicalAnd();
            v = (v || r) ? 1 : 0;
        }
        return v;
    }
    int64_t logicalAnd() {
        int64_t v = bitOr();
        while (!failed_ && accept("&&")) {
            int64_t r = bitOr();
            v = (v && r) ? 1 : 0;
        }
        return v;
    }
    int64_t bitOr() {
        int64_t v = bitXor();
        while (!failed_ && !peek("||") && accept("|")) v |= bitXor();
        return v;
    }
    int64_t bitXor() {
        int64_t v = bitAnd();
        while (!failed_ && accept("^")) v ^= bitAnd();
        return v;
    }
    int64_t bitAnd() {
        int64_t v = equality();
        while (!failed_ && !peek("&&") && accept("&")) v &= equality();
        return v;
    }
    int64_t equality() {
        int64_t v = relational();
        while (!failed_) {
            if (accept("==")) v = (v == relational()) ? 1 : 0;
            else if (accept("!=")) v = (v != relational()) ? 1 : 0;
            else break;
        }
        return v;
    }
    int64_t relational() {
        int64_t v = shift();
        while (!failed_) {
            if (accept("<=")) v = (v <= shift()) ? 1 : 0;
            else if (accept(">=")) v = (v >= shift()) ? 1 : 0;
            else if (!peek("<<") && accept("<")) v = (v < shift()) ? 1 : 0;
            else if (!peek(">>") && accept(">")) v = (v > shift()) ? 1 : 0;
            else break;
        }
        return v;
    }
    int64_t shift() {
        int64_t v = additive();
        while (!failed_) {
            if (accept("<<")) {
                int64_t s = additive();
                v = (s >= 0 && s < 63) ? int64_t(uint64_t(v) << s) : 0;
            } else if (accept(">>")) {
                int64_t s = additive();
                v = (s >= 0 && s < 63) ? (v >> s) : 0;
            } else {
                break;
            }
        }
        return v;
    }
    int64_t additive() {
        int64_t v = multiplicative();
        while (!failed_) {
            if (accept("+")) v = int64_t(uint64_t(v) + uint64_t(multiplicative()));
            else if (accept("-")) v = int64_t(uint64_t(v) - uint64_t(multiplicative()));
            else break;
        }
        return v;
    }
    int64_t multiplicative() {
        int64_t v = unary();
        while (!failed_) {
            if (accept("*")) {
                v = int64_t(uint64_t(v) * uint64_t(unary()));
            } else if (accept("/")) {
                int64_t d = unary();
                v = (d == 0 || (d == -1 && v == INT64_MIN)) ? 0 : v / d;
            } else if (accept("%")) {
                int64_t d = unary();
                v = (d == 0 || d == -1) ? 0 : v % d;
            } else {
                break;
            }
        }
        return v;
    }
    int64_t unary() {
        if (accept("!")) return unary() ? 0 : 1;
        if (accept("~")) return ~unary();
        if (accept("-")) return int64_t(0 - uint64_t(unary()));
        if (accept("+")) return unary();
        return primary();
    }
    int64_t primary() {
        skip();
        if (pos_ >= text_.size()) return fail();
        if (accept("(")) {
            int64_t v = ternary();
            if (!accept(")")) return fail();
            return v;
        }
        char c = text_[pos_];
        if (std::isdigit(static_cast<unsigned char>(c))) return number();
        if (identStart(c)) {
            std::string name = identifier();
            if (name == "defined") {
                bool paren = accept("(");
                skip();
                if (pos_ >= text_.size() || !identStart(text_[pos_])) return fail();
                std::string target = identifier();
                if (paren && !accept(")")) return fail();
                bool function = false;
                return lookup_(target, function).has_value() ? 1 : 0;
            }
            bool function = false;
            auto value = lookup_(name, function);
            if (!value) return 0;  // undefined names are 0
            if (function) {        // function like macros are not evaluated
                if (accept("(")) skipBalanced();
                return 0;
            }
            if (depth_ >= 32) return fail();
            std::string inner(trimView(*value));
            if (inner.empty()) return 0;
            auto v = Expression(inner, lookup_, depth_ + 1).run();
            return v.value_or(0);
        }
        return fail();
    }
    std::string identifier() {
        size_t start = pos_;
        while (pos_ < text_.size() && identChar(text_[pos_])) pos_++;
        return std::string(text_.substr(start, pos_ - start));
    }
    int64_t number() {
        int64_t v = 0;
        int base = 10;
        if (text_.compare(pos_, 2, "0x") == 0 || text_.compare(pos_, 2, "0X") == 0) {
            base = 16;
            pos_ += 2;
        }
        bool any = false;
        while (pos_ < text_.size()) {
            char c = static_cast<char>(std::tolower(static_cast<unsigned char>(text_[pos_])));
            int digit = std::isdigit(static_cast<unsigned char>(c)) ? c - '0'
                        : (base == 16 && c >= 'a' && c <= 'f') ? c - 'a' + 10
                                                                : -1;
            if (digit < 0) break;
            v = int64_t(uint64_t(v) * uint64_t(base) + uint64_t(digit));
            any = true;
            pos_++;
        }
        if (!any) return fail();
        if (pos_ < text_.size() && text_[pos_] == '.') {  // "1.0": integer part
            pos_++;
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) pos_++;
        }
        while (pos_ < text_.size() && std::strchr("uUlLfF", text_[pos_]) && text_[pos_] != '\0') pos_++;
        return v;
    }
    void skipBalanced() {
        int level = 1;
        while (pos_ < text_.size() && level > 0) {
            if (text_[pos_] == '(') level++;
            else if (text_[pos_] == ')') level--;
            pos_++;
        }
    }

    std::string_view text_;
    const Lookup& lookup_;
    int depth_;
    size_t pos_ = 0;
    bool failed_ = false;
};

}  // namespace

ShaderPreprocessor::ShaderPreprocessor(ShaderLoader loader,
                                       std::map<std::string, std::string> defines)
    : loader_(std::move(loader)) {
    for (auto& [name, value] : defines) macros_[name] = Macro{false, std::move(value)};
}

std::optional<int64_t> ShaderPreprocessor::evaluate(std::string_view expression) const {
    Expression::Lookup lookup = [this](const std::string& name,
                                       bool& function) -> std::optional<std::string> {
        auto it = macros_.find(name);
        if (it == macros_.end()) return std::nullopt;
        function = it->second.function;
        return it->second.value;
    };
    std::string clean = stripComments(expression);
    return Expression(clean, lookup, 0).run();
}

PreprocessResult ShaderPreprocessor::run(const std::string& path) {
    result_ = PreprocessResult{};
    included_.clear();
    conditions_.clear();
    auto text = loader_(path);
    if (!text) {
        result_.error = path + ": not found";
        return result_;
    }
    included_.insert(path);
    processFile(path, normalizeLineEnds(*text), 0);
    if (!conditions_.empty()) {
        result_.notes.push_back("closed " + std::to_string(conditions_.size()) +
                                " unterminated #if block(s)");
        conditions_.clear();
    }
    for (const auto& [name, macro] : macros_)
        if (!macro.function) result_.macros[name] = macro.value;
    return result_;
}

void ShaderPreprocessor::processFile(const std::string& path, const std::string& text, int depth) {
    bool inBlock = false;
    size_t pos = 0;
    while (pos < text.size() && result_.error.empty()) {
        // One logical line: physical lines joined at trailing backslashes.
        // A final line break does not start another line.
        std::string line;
        while (true) {
            size_t end = text.find('\n', pos);
            bool last = end == std::string::npos;
            if (last) end = text.size();
            std::string_view part(text.data() + pos, end - pos);
            pos = last ? text.size() : end + 1;
            if (!part.empty() && part.back() == '\\' && pos < text.size()) {
                line.append(part.substr(0, part.size() - 1));
                continue;
            }
            line.append(part);
            break;
        }
        bool startsInBlock = inBlock;
        inBlock = blockCommentAfter(line, inBlock);
        std::string_view view = trimView(line);
        if (!startsInBlock && !view.empty() && view.front() == '#') {
            std::string_view after = trimView(view.substr(1));
            size_t n = 0;
            while (n < after.size() && identChar(after[n])) n++;
            directive(line, after.substr(0, n), trimView(after.substr(n)), depth);
            continue;
        }
        if (active()) {
            result_.text.append(line);
            result_.text.push_back('\n');
        }
    }
    (void)path;
}

void ShaderPreprocessor::directive(const std::string& line, std::string_view name,
                                   std::string_view rest, int depth) {
    auto value = [&](std::string_view expr) -> int64_t {
        auto v = evaluate(expr);
        if (!v) result_.notes.push_back("unreadable #if expression treated as false");
        return v.value_or(0);
    };
    auto firstWord = [](std::string_view s) {
        size_t n = 0;
        while (n < s.size() && identChar(s[n])) n++;
        return std::string(s.substr(0, n));
    };

    if (name == "if" || name == "ifdef" || name == "ifndef") {
        Conditional c;
        c.parentActive = active();
        bool on = false;
        if (c.parentActive) {
            if (name == "if") on = value(rest) != 0;
            else {
                bool defined = macros_.count(firstWord(rest)) > 0;
                on = name == "ifdef" ? defined : !defined;
            }
        }
        c.active = c.parentActive && on;
        c.taken = c.active;
        conditions_.push_back(c);
        return;
    }
    if (name == "elif") {
        if (conditions_.empty()) {
            result_.notes.push_back("stray #elif ignored");
            return;
        }
        Conditional& c = conditions_.back();
        if (c.sawElse || c.taken || !c.parentActive) {
            c.active = false;
        } else {
            c.active = value(rest) != 0;
            c.taken = c.active;
        }
        return;
    }
    if (name == "else") {
        if (conditions_.empty()) {
            result_.notes.push_back("stray #else ignored");
            return;
        }
        Conditional& c = conditions_.back();
        c.active = c.parentActive && !c.taken && !c.sawElse;
        c.taken = true;
        c.sawElse = true;
        return;
    }
    if (name == "endif") {
        if (conditions_.empty()) {
            if (active()) result_.text.append("// desktop-canvas: unmatched #endif removed\n");
            result_.notes.push_back("unmatched #endif removed");
            return;
        }
        conditions_.pop_back();
        return;
    }
    if (!active()) return;

    if (name == "define") {
        std::string macro = firstWord(rest);
        if (macro.empty()) {
            result_.notes.push_back("#define without a name dropped");
            return;
        }
        Macro m;
        std::string_view after = rest.substr(macro.size());
        m.function = !after.empty() && after.front() == '(';
        m.value = std::string(trimView(stripComments(after)));
        macros_[macro] = std::move(m);
        result_.text.append(line);
        result_.text.push_back('\n');
        return;
    }
    if (name == "undef") {
        macros_.erase(firstWord(rest));
        result_.text.append(line);
        result_.text.push_back('\n');
        return;
    }
    if (name == "include") {
        std::string stripped = stripComments(rest);  // the view below points into it
        std::string_view target = trimView(stripped);
        if (target.size() < 2 || !((target.front() == '"' && target.back() == '"') ||
                                   (target.front() == '<' && target.back() == '>'))) {
            result_.error = "malformed #include";
            return;
        }
        std::string file(target.substr(1, target.size() - 2));
        if (file.empty() || file.front() == '/' || file.find("..") != std::string::npos) {
            result_.error = "#include \"" + file + "\" is not a plain path";
            return;
        }
        std::string path = "shaders/" + file;
        if (included_.count(path)) return;  // once per program
        if (depth + 1 > kMaxIncludeDepth) {
            result_.error = "includes nested deeper than 16";
            return;
        }
        auto text = loader_(path);
        if (!text) {
            result_.error = path + ": not found";
            return;
        }
        included_.insert(path);
        result_.includes.push_back(path);
        processFile(path, normalizeLineEnds(*text), depth + 1);
        return;
    }
    if (name == "extension") {
        result_.extensions.push_back(std::string(trimView(line)));
        return;
    }
    if (name == "pragma" || name == "line" || name == "error") {
        result_.text.append(line);
        result_.text.push_back('\n');
        return;
    }
    if (name == "require") {
        result_.requirements.push_back(firstWord(rest));
        return;
    }
    // #version (the translator writes its own) and anything else.
    result_.notes.push_back("#" + std::string(name) + " dropped");
}

}  // namespace canvas::scene
