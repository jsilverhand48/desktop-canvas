// Implementation of the tokenizer and macro expander declared in Tokens.h.
#include "scene/shader/Tokens.h"

#include <cctype>
#include <cstring>
#include <map>
#include <set>

namespace canvas::scene {

namespace {

bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Comments become spaces; newlines inside block comments stay.
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

const char* const kPuncts[] = {"<<=", ">>=", "++", "--", "<<", ">>", "<=", ">=", "==", "!=",
                               "&&",  "||",  "^^", "+=", "-=", "*=", "/=", "%=", "&=", "^=",
                               "|="};

bool lexInto(const std::string& code, int line, std::vector<Token>& out, std::string& error) {
    size_t i = 0;
    while (i < code.size()) {
        char c = code[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (c == '\n') line++;
            i++;
            continue;
        }
        Token t;
        t.line = line;
        if (identStart(c)) {
            size_t j = i;
            while (j < code.size() && identChar(code[j])) j++;
            t.kind = TokenKind::Identifier;
            t.text = code.substr(i, j - i);
            i = j;
        } else if (std::isdigit(static_cast<unsigned char>(c)) ||
                   (c == '.' && i + 1 < code.size() &&
                    std::isdigit(static_cast<unsigned char>(code[i + 1])))) {
            size_t j = i;
            bool isFloat = false;
            if (code.compare(i, 2, "0x") == 0 || code.compare(i, 2, "0X") == 0) {
                j += 2;
                while (j < code.size() && std::isxdigit(static_cast<unsigned char>(code[j]))) j++;
            } else {
                while (j < code.size() && std::isdigit(static_cast<unsigned char>(code[j]))) j++;
                if (j < code.size() && code[j] == '.') {
                    isFloat = true;
                    j++;
                    while (j < code.size() && std::isdigit(static_cast<unsigned char>(code[j]))) j++;
                }
                if (j < code.size() && (code[j] == 'e' || code[j] == 'E')) {
                    size_t k = j + 1;
                    if (k < code.size() && (code[k] == '+' || code[k] == '-')) k++;
                    if (k < code.size() && std::isdigit(static_cast<unsigned char>(code[k]))) {
                        isFloat = true;
                        j = k;
                        while (j < code.size() && std::isdigit(static_cast<unsigned char>(code[j]))) j++;
                    }
                }
            }
            t.text = code.substr(i, j - i);
            t.kind = isFloat ? TokenKind::Float : TokenKind::Int;
            if (j < code.size() && (code[j] == 'u' || code[j] == 'U')) {
                if (t.kind == TokenKind::Int) t.kind = TokenKind::Uint;
                t.text += code[j++];
            } else if (j < code.size() && (code[j] == 'f' || code[j] == 'F')) {
                t.kind = TokenKind::Float;
                t.text += code[j++];
            }
            i = j;
        } else {
            t.kind = TokenKind::Punct;
            bool matched = false;
            for (const char* p : kPuncts) {
                size_t n = std::strlen(p);
                if (code.compare(i, n, p) == 0) {
                    t.text = p;
                    i += n;
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                if (!std::strchr("(){}[];,.+-*/%<>=!~&|^?:", c)) {
                    error = "line " + std::to_string(line) + ": unexpected character '" +
                            std::string(1, c) + "'";
                    return false;
                }
                t.text = std::string(1, c);
                i++;
            }
        }
        out.push_back(std::move(t));
    }
    return true;
}

struct Macro {
    bool function = false;
    std::vector<std::string> params;
    std::vector<Token> body;
};

class Expander {
public:
    explicit Expander(const std::map<std::string, Macro>& macros) : macros_(macros) {}

    // C rules: a replacement is rescanned together with the tokens after
    // it (so `#define A B` then `A(x)` calls a function like macro B);
    // every token remembers the macros it came from, which do not expand
    // in it again.
    bool expand(const std::vector<Token>& in, std::vector<Token>& out) {
        std::vector<Item> buf;
        buf.reserve(in.size());
        for (const auto& t : in) buf.push_back({t, {}});
        size_t i = 0;
        while (i < buf.size()) {
            if (++steps_ > 200000) {
                error = "macro expansion does not end";
                return false;
            }
            const Item& item = buf[i];
            const Token& t = item.token;
            auto it = t.kind == TokenKind::Identifier ? macros_.find(t.text) : macros_.end();
            if (it == macros_.end() || hidden(item, t.text)) {
                out.push_back(t);
                i++;
                continue;
            }
            const Macro& m = it->second;
            std::vector<std::string> hide = item.hide;
            hide.push_back(t.text);
            if (!m.function) {
                std::vector<Item> repl;
                for (Token b : m.body) {
                    b.line = t.line;
                    repl.push_back({b, hide});
                }
                buf.erase(buf.begin() + long(i));
                buf.insert(buf.begin() + long(i), repl.begin(), repl.end());
                continue;
            }
            if (i + 1 >= buf.size() || buf[i + 1].token.text != "(") {
                out.push_back(t);  // a function like macro's name alone
                i++;
                continue;
            }
            std::vector<std::vector<Token>> args(1);
            int level = 0;
            size_t j = i + 2;
            for (; j < buf.size(); j++) {
                const Token& a = buf[j].token;
                if (a.kind == TokenKind::Punct && (a.text == "(" || a.text == "[")) level++;
                if (a.kind == TokenKind::Punct && (a.text == ")" || a.text == "]")) {
                    if (level == 0 && a.text == ")") break;
                    level--;
                }
                if (level == 0 && a.kind == TokenKind::Punct && a.text == ",") {
                    args.emplace_back();
                    continue;
                }
                args.back().push_back(a);
            }
            if (j >= buf.size()) {
                error = "line " + std::to_string(t.line) + ": unterminated call of macro " + t.text;
                return false;
            }
            if (args.size() == 1 && args[0].empty() && m.params.empty()) args.clear();
            if (args.size() != m.params.size()) {
                error = "line " + std::to_string(t.line) + ": macro " + t.text + " takes " +
                        std::to_string(m.params.size()) + " arguments";
                return false;
            }
            // Arguments are expanded on their own before substitution.
            std::vector<std::vector<Token>> expanded;
            for (const auto& a : args) {
                std::vector<Token> e;
                Expander inner(macros_);
                inner.steps_ = steps_;
                if (!inner.expand(a, e)) {
                    error = inner.error;
                    return false;
                }
                steps_ = inner.steps_;
                expanded.push_back(std::move(e));
            }
            std::vector<Item> repl;
            for (const Token& b : m.body) {
                bool replaced = false;
                if (b.kind == TokenKind::Identifier)
                    for (size_t p = 0; p < m.params.size(); p++)
                        if (m.params[p] == b.text) {
                            for (const Token& a : expanded[p]) repl.push_back({a, hide});
                            replaced = true;
                            break;
                        }
                if (!replaced) {
                    Token copy = b;
                    copy.line = t.line;
                    repl.push_back({copy, hide});
                }
            }
            buf.erase(buf.begin() + long(i), buf.begin() + long(j) + 1);
            buf.insert(buf.begin() + long(i), repl.begin(), repl.end());
        }
        return true;
    }

    std::string error;

private:
    struct Item {
        Token token;
        std::vector<std::string> hide;
    };

    static bool hidden(const Item& item, const std::string& name) {
        for (const auto& h : item.hide)
            if (h == name) return true;
        return false;
    }

    const std::map<std::string, Macro>& macros_;
    size_t steps_ = 0;
};

}  // namespace

std::vector<Token> tokenize(const std::string& code, std::string& error) {
    std::vector<Token> out;
    if (!lexInto(blankComments(code), 1, out, error)) out.clear();
    return out;
}

std::optional<ExpandedProgram> expandProgram(const std::string& program, std::string& error) {
    ExpandedProgram result;
    std::map<std::string, Macro> macros;
    std::string text = blankComments(program);
    std::vector<Token> chunk;  // code since the last directive
    auto flush = [&]() -> bool {
        Expander e(macros);
        if (!e.expand(chunk, result.tokens)) {
            error = e.error;
            return false;
        }
        chunk.clear();
        return true;
    };
    size_t pos = 0;
    int line = 1;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string logical = text.substr(pos, end - pos);
        int firstLine = line;
        pos = end + 1;
        line++;
        while (!logical.empty() && logical.back() == '\\' && pos < text.size()) {
            logical.pop_back();
            size_t e2 = text.find('\n', pos);
            if (e2 == std::string::npos) e2 = text.size();
            logical += text.substr(pos, e2 - pos);
            pos = e2 + 1;
            line++;
        }
        size_t first = logical.find_first_not_of(" \t\r");
        if (first == std::string::npos || logical[first] != '#') {
            if (!lexInto(logical + "\n", firstLine, chunk, error)) return std::nullopt;
            continue;
        }
        std::string directive = logical.substr(first + 1);
        size_t s = directive.find_first_not_of(" \t");
        directive = s == std::string::npos ? "" : directive.substr(s);
        size_t n = 0;
        while (n < directive.size() && identChar(directive[n])) n++;
        std::string name = directive.substr(0, n);
        std::string rest = directive.substr(n);
        if (name == "version") {
            result.version = "#" + directive;
        } else if (name == "extension") {
            result.extensions.push_back("#" + directive);
        } else if (name == "define" || name == "undef") {
            if (!flush()) return std::nullopt;
            size_t a = rest.find_first_not_of(" \t");
            if (a == std::string::npos) continue;
            size_t b = a;
            while (b < rest.size() && identChar(rest[b])) b++;
            std::string macro = rest.substr(a, b - a);
            if (name == "undef") {
                macros.erase(macro);
                continue;
            }
            Macro m;
            std::string body = rest.substr(b);
            if (!body.empty() && body.front() == '(') {
                size_t close = body.find(')');
                if (close == std::string::npos) {
                    error = "line " + std::to_string(firstLine) + ": malformed macro " + macro;
                    return std::nullopt;
                }
                m.function = true;
                std::string params = body.substr(1, close - 1);
                size_t p = 0;
                while (p < params.size()) {
                    size_t comma = params.find(',', p);
                    if (comma == std::string::npos) comma = params.size();
                    std::string param = params.substr(p, comma - p);
                    size_t l = param.find_first_not_of(" \t");
                    size_t r = param.find_last_not_of(" \t");
                    if (l != std::string::npos) m.params.push_back(param.substr(l, r - l + 1));
                    p = comma + 1;
                }
                body = body.substr(close + 1);
            }
            if (!lexInto(body, firstLine, m.body, error)) return std::nullopt;
            macros[macro] = std::move(m);
        } else if (name == "line" || name == "pragma") {
            // not needed in the printed program
        } else {
            error = "line " + std::to_string(firstLine) + ": unexpected #" + name;
            return std::nullopt;
        }
    }
    if (!flush()) return std::nullopt;
    return result;
}

}  // namespace canvas::scene
