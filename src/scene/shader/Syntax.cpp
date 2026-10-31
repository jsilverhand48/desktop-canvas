// Implementation of the GLSL types, parser and printer declared in
// Syntax.h.
#include "scene/shader/Syntax.h"

#include <set>

namespace canvas::scene {

using Base = GlslType::Base;

std::string GlslType::glsl() const {
    switch (base) {
        case Base::Void: return "void";
        case Base::Sampler:
        case Base::Struct: return name;
        case Base::Unknown: return "?";
        default: break;
    }
    if (columns > 0) {
        if (columns == size) return "mat" + std::to_string(columns);
        return "mat" + std::to_string(columns) + "x" + std::to_string(size);
    }
    const char* scalarName = base == Base::Bool ? "bool" : base == Base::Int ? "int"
                             : base == Base::Uint ? "uint" : "float";
    if (size == 1) return scalarName;
    const char* prefix = base == Base::Bool ? "bvec" : base == Base::Int ? "ivec"
                         : base == Base::Uint ? "uvec" : "vec";
    return prefix + std::to_string(size);
}

GlslType GlslType::element() const {
    GlslType t = *this;
    t.array = 0;
    return t;
}

bool GlslType::operator==(const GlslType& o) const {
    return base == o.base && size == o.size && columns == o.columns && name == o.name &&
           array == o.array;
}

GlslType builtinType(const std::string& n) {
    if (n == "void") return GlslType::scalar(Base::Void);
    if (n == "bool") return GlslType::scalar(Base::Bool);
    if (n == "int") return GlslType::scalar(Base::Int);
    if (n == "uint") return GlslType::scalar(Base::Uint);
    if (n == "float") return GlslType::scalar(Base::Float);
    auto vec = [&](const char* prefix, Base b) -> GlslType {
        size_t p = std::char_traits<char>::length(prefix);
        if (n.size() == p + 1 && n.compare(0, p, prefix) == 0 && n[p] >= '2' && n[p] <= '4')
            return GlslType::vector(b, n[p] - '0');
        return {};
    };
    for (auto [prefix, b] : {std::pair{"vec", Base::Float}, {"ivec", Base::Int},
                             {"uvec", Base::Uint}, {"bvec", Base::Bool}}) {
        GlslType t = vec(prefix, b);
        if (t.known()) return t;
    }
    if (n.size() >= 4 && n.compare(0, 3, "mat") == 0) {
        GlslType t = GlslType::scalar(Base::Float);
        if (n.size() == 4 && n[3] >= '2' && n[3] <= '4') {
            t.columns = t.size = n[3] - '0';
            return t;
        }
        if (n.size() == 6 && n[4] == 'x' && n[3] >= '2' && n[3] <= '4' && n[5] >= '2' &&
            n[5] <= '4') {
            t.columns = n[3] - '0';
            t.size = n[5] - '0';
            return t;
        }
    }
    if (n.find("sampler") != std::string::npos &&
        (n.rfind("sampler", 0) == 0 || n.rfind("isampler", 0) == 0 ||
         n.rfind("usampler", 0) == 0)) {
        GlslType t;
        t.base = Base::Sampler;
        t.name = n;
        return t;
    }
    return {};
}

namespace {

const std::set<std::string> kQualifiers = {
    "const", "uniform", "in", "out", "inout", "attribute", "varying", "centroid", "flat",
    "smooth", "noperspective", "invariant", "highp", "mediump", "lowp", "precise"};

class Parser {
public:
    explicit Parser(const std::vector<Token>& t) : t_(t) {}

    std::optional<TranslationUnit> run(std::string& error) {
        TranslationUnit unit;
        while (!failed_ && pos_ < t_.size()) {
            TopLevel item;
            if (peekIs("precision")) {
                item.kind = TopLevel::Kind::Precision;
                std::string text;
                while (pos_ < t_.size() && !peekIs(";")) text += (text.empty() ? "" : " ") + t_[pos_++].text;
                expect(";");
                item.text = text + ";";
            } else if (peekIs(";")) {
                pos_++;
                continue;
            } else {
                std::vector<std::string> qualifiers = parseQualifiers();
                TypeSpec type;
                if (!parseTypeSpec(type)) break;
                if (isIdentifier() && peekIs("(", 1)) {
                    item.kind = TopLevel::Kind::Function;
                    item.function = parseFunction(std::move(qualifiers), std::move(type));
                } else {
                    item.kind = TopLevel::Kind::Declaration;
                    item.decl = parseDeclarationRest(std::move(qualifiers), std::move(type));
                }
            }
            if (!failed_) unit.items.push_back(std::move(item));
        }
        if (failed_) {
            error = error_;
            return std::nullopt;
        }
        return unit;
    }

private:
    // --- helpers -------------------------------------------------------------
    bool at(size_t offset = 0) const { return pos_ + offset < t_.size(); }
    bool peekIs(const char* text, size_t offset = 0) const {
        return at(offset) && t_[pos_ + offset].text == text &&
               (t_[pos_ + offset].kind == TokenKind::Punct ||
                t_[pos_ + offset].kind == TokenKind::Identifier);
    }
    bool isIdentifier(size_t offset = 0) const {
        return at(offset) && t_[pos_ + offset].kind == TokenKind::Identifier;
    }
    int line() const { return at() ? t_[pos_].line : (t_.empty() ? 0 : t_.back().line); }
    void fail(const std::string& why) {
        if (failed_) return;
        failed_ = true;
        error_ = "line " + std::to_string(line()) + ": " + why +
                 (at() ? " near '" + t_[pos_].text + "'" : " at the end");
    }
    bool expect(const char* text) {
        if (!peekIs(text)) {
            fail(std::string("expected '") + text + "'");
            return false;
        }
        pos_++;
        return true;
    }
    bool isTypeName(size_t offset = 0) const {
        if (!isIdentifier(offset)) return false;
        const std::string& n = t_[pos_ + offset].text;
        return builtinType(n).known() || structs_.count(n) || n == "struct";
    }

    std::vector<std::string> parseQualifiers() {
        std::vector<std::string> q;
        while (!failed_ && at()) {
            if (peekIs("layout") && peekIs("(", 1)) {
                std::string text = "layout(";
                pos_ += 2;
                int level = 1;
                while (at() && level > 0) {
                    if (peekIs("(")) level++;
                    if (peekIs(")") && --level == 0) break;
                    text += t_[pos_].text;
                    if (t_[pos_].text == ",") text += " ";
                    else if (at(1) && t_[pos_ + 1].text == "=") text += " ";
                    else if (t_[pos_].text == "=") text += " ";
                    pos_++;
                }
                expect(")");
                q.push_back(text + ")");
            } else if (isIdentifier() && kQualifiers.count(t_[pos_].text)) {
                q.push_back(t_[pos_++].text);
            } else {
                break;
            }
        }
        return q;
    }

    bool parseTypeSpec(TypeSpec& type) {
        if (peekIs("struct")) {
            pos_++;
            auto def = std::make_shared<StructDef>();
            if (isIdentifier()) def->name = t_[pos_++].text;
            if (!expect("{")) return false;
            while (!failed_ && at() && !peekIs("}")) {
                std::vector<std::string> q = parseQualifiers();
                TypeSpec memberType;
                if (!parseTypeSpec(memberType)) return false;
                auto member = parseDeclarationRest(std::move(q), std::move(memberType));
                if (member) def->members.push_back(std::move(*member));
            }
            if (!expect("}")) return false;
            if (!def->name.empty()) structs_.insert(def->name);
            type.name = def->name;
            type.structDef = def;
            return true;
        }
        if (!isTypeName()) {
            fail("expected a type");
            return false;
        }
        type.name = t_[pos_++].text;
        if (peekIs("[") ) {  // float[3] name
            pos_++;
            type.hasArray = true;
            if (!peekIs("]")) type.arraySize = parseAssignment();
            if (!expect("]")) return false;
        }
        return true;
    }

    std::unique_ptr<Declaration> parseDeclarationRest(std::vector<std::string> qualifiers,
                                                      TypeSpec type) {
        auto d = std::make_unique<Declaration>();
        d->line = line();
        d->qualifiers = std::move(qualifiers);
        d->type = std::move(type);
        if (peekIs(";")) {
            pos_++;
            return d;
        }
        while (!failed_) {
            if (!isIdentifier()) {
                fail("expected a name");
                return d;
            }
            Declarator v;
            v.name = t_[pos_++].text;
            if (peekIs("[")) {
                pos_++;
                v.hasArray = true;
                if (!peekIs("]")) v.arraySize = parseAssignment();
                if (!expect("]")) return d;
            }
            if (peekIs("=")) {
                pos_++;
                v.init = parseAssignment();
            }
            d->vars.push_back(std::move(v));
            if (peekIs(",")) {
                pos_++;
                continue;
            }
            expect(";");
            return d;
        }
        return d;
    }

    std::unique_ptr<Function> parseFunction(std::vector<std::string> qualifiers, TypeSpec type) {
        auto f = std::make_unique<Function>();
        f->line = line();
        f->qualifiers = std::move(qualifiers);
        f->returnType = std::move(type);
        f->name = t_[pos_++].text;
        expect("(");
        if (peekIs("void") && peekIs(")", 1)) pos_++;
        while (!failed_ && at() && !peekIs(")")) {
            Parameter p;
            p.qualifiers = parseQualifiers();
            if (!parseTypeSpec(p.type)) return f;
            if (isIdentifier()) p.name = t_[pos_++].text;
            if (peekIs("[")) {
                pos_++;
                p.hasArray = true;
                if (!peekIs("]")) p.arraySize = parseAssignment();
                expect("]");
            }
            f->params.push_back(std::move(p));
            if (!peekIs(",")) break;
            pos_++;
        }
        expect(")");
        if (peekIs(";")) {
            pos_++;
            return f;
        }
        f->body = parseBlock();
        return f;
    }

    // --- statements --------------------------------------------------------
    StmtPtr parseBlock() {
        auto s = std::make_unique<Stmt>();
        s->kind = Stmt::Kind::Block;
        s->line = line();
        if (!expect("{")) return s;
        while (!failed_ && at() && !peekIs("}")) s->body.push_back(parseStatement());
        expect("}");
        return s;
    }

    bool startsDeclaration() const {
        if (!isIdentifier()) return false;
        const std::string& n = t_[pos_].text;
        if (kQualifiers.count(n) || n == "struct" || n == "layout") return true;
        if (!isTypeName()) return false;
        if (isIdentifier(1)) return true;  // vec3 x
        // vec3[2] x
        return peekIs("[", 1);
    }

    StmtPtr parseStatement() {
        auto s = std::make_unique<Stmt>();
        s->line = line();
        if (peekIs("{")) return parseBlock();
        if (peekIs(";")) {
            pos_++;
            s->kind = Stmt::Kind::Empty;
            return s;
        }
        if (peekIs("if")) {
            pos_++;
            s->kind = Stmt::Kind::If;
            expect("(");
            s->expr = parseExpression();
            expect(")");
            s->body.push_back(parseStatement());
            if (peekIs("else")) {
                pos_++;
                s->body.push_back(parseStatement());
            }
            return s;
        }
        if (peekIs("for")) {
            pos_++;
            s->kind = Stmt::Kind::For;
            expect("(");
            if (peekIs(";")) {
                pos_++;
            } else if (startsDeclaration()) {
                auto init = std::make_unique<Stmt>();
                init->kind = Stmt::Kind::Declaration;
                init->line = line();
                std::vector<std::string> q = parseQualifiers();
                TypeSpec type;
                if (!parseTypeSpec(type)) return s;
                init->decl = parseDeclarationRest(std::move(q), std::move(type));
                s->init = std::move(init);
            } else {
                auto init = std::make_unique<Stmt>();
                init->kind = Stmt::Kind::Expression;
                init->expr = parseExpression();
                expect(";");
                s->init = std::move(init);
            }
            if (!peekIs(";")) s->expr = parseExpression();
            expect(";");
            if (!peekIs(")")) s->step = parseExpression();
            expect(")");
            s->body.push_back(parseStatement());
            return s;
        }
        if (peekIs("while")) {
            pos_++;
            s->kind = Stmt::Kind::While;
            expect("(");
            s->expr = parseExpression();
            expect(")");
            s->body.push_back(parseStatement());
            return s;
        }
        if (peekIs("do")) {
            pos_++;
            s->kind = Stmt::Kind::DoWhile;
            s->body.push_back(parseStatement());
            expect("while");
            expect("(");
            s->expr = parseExpression();
            expect(")");
            expect(";");
            return s;
        }
        if (peekIs("return")) {
            pos_++;
            s->kind = Stmt::Kind::Return;
            if (!peekIs(";")) s->expr = parseExpression();
            expect(";");
            return s;
        }
        for (auto [word, kind] : {std::pair{"break", Stmt::Kind::Break},
                                  {"continue", Stmt::Kind::Continue},
                                  {"discard", Stmt::Kind::Discard}}) {
            if (peekIs(word)) {
                pos_++;
                s->kind = kind;
                expect(";");
                return s;
            }
        }
        if (peekIs("switch") || peekIs("case") || peekIs("default")) {
            fail("switch statements are not supported");
            return s;
        }
        if (startsDeclaration()) {
            s->kind = Stmt::Kind::Declaration;
            std::vector<std::string> q = parseQualifiers();
            TypeSpec type;
            if (!parseTypeSpec(type)) return s;
            s->decl = parseDeclarationRest(std::move(q), std::move(type));
            return s;
        }
        s->kind = Stmt::Kind::Expression;
        s->expr = parseExpression();
        expect(";");
        return s;
    }

    // --- expressions ---------------------------------------------------------
    ExprPtr make(Expr::Kind kind, std::string text, int ln) {
        auto e = std::make_unique<Expr>();
        e->kind = kind;
        e->text = std::move(text);
        e->line = ln;
        return e;
    }

    ExprPtr parseExpression() {
        ExprPtr left = parseAssignment();
        if (!peekIs(",")) return left;
        auto comma = make(Expr::Kind::Comma, ",", line());
        comma->children.push_back(std::move(left));
        while (!failed_ && peekIs(",")) {
            pos_++;
            comma->children.push_back(parseAssignment());
        }
        return comma;
    }

    ExprPtr parseAssignment() {
        ExprPtr left = parseTernary();
        static const std::set<std::string> ops = {"=",  "+=", "-=", "*=",  "/=",  "%=",
                                                  "<<=", ">>=", "&=", "^=", "|="};
        if (at() && t_[pos_].kind == TokenKind::Punct && ops.count(t_[pos_].text)) {
            auto e = make(Expr::Kind::Assign, t_[pos_].text, line());
            pos_++;
            e->children.push_back(std::move(left));
            e->children.push_back(parseAssignment());
            return e;
        }
        return left;
    }

    ExprPtr parseTernary() {
        ExprPtr cond = parseBinary(0);
        if (!peekIs("?")) return cond;
        auto e = make(Expr::Kind::Ternary, "?", line());
        pos_++;
        e->children.push_back(std::move(cond));
        e->children.push_back(parseAssignment());
        expect(":");
        e->children.push_back(parseAssignment());
        return e;
    }

    static int precedence(const std::string& op) {
        static const std::vector<std::set<std::string>> levels = {
            {"||"}, {"^^"}, {"&&"}, {"|"}, {"^"}, {"&"}, {"==", "!="}, {"<", ">", "<=", ">="},
            {"<<", ">>"}, {"+", "-"}, {"*", "/", "%"}};
        for (size_t i = 0; i < levels.size(); i++)
            if (levels[i].count(op)) return int(i);
        return -1;
    }

    ExprPtr parseBinary(int minLevel) {
        ExprPtr left = parseUnary();
        while (!failed_ && at() && t_[pos_].kind == TokenKind::Punct) {
            int level = precedence(t_[pos_].text);
            if (level < minLevel || level < 0) break;
            auto e = make(Expr::Kind::Binary, t_[pos_].text, line());
            pos_++;
            e->children.push_back(std::move(left));
            e->children.push_back(parseBinary(level + 1));
            left = std::move(e);
        }
        return left;
    }

    ExprPtr parseUnary() {
        if (at() && t_[pos_].kind == TokenKind::Punct) {
            const std::string& op = t_[pos_].text;
            if (op == "+" || op == "-" || op == "!" || op == "~" || op == "++" || op == "--") {
                auto e = make(Expr::Kind::Unary, op, line());
                pos_++;
                e->children.push_back(parseUnary());
                return e;
            }
        }
        return parsePostfix(parsePrimary());
    }

    ExprPtr parsePostfix(ExprPtr e) {
        while (!failed_ && at()) {
            if (peekIs("[")) {
                auto idx = make(Expr::Kind::Index, "[", line());
                pos_++;
                idx->children.push_back(std::move(e));
                idx->children.push_back(parseExpression());
                expect("]");
                e = std::move(idx);
            } else if (peekIs(".")) {
                pos_++;
                if (!isIdentifier()) {
                    fail("expected a member name");
                    return e;
                }
                auto m = make(Expr::Kind::Member, t_[pos_++].text, line());
                m->children.push_back(std::move(e));
                e = std::move(m);
            } else if (peekIs("++") || peekIs("--")) {
                auto p = make(Expr::Kind::Postfix, t_[pos_].text, line());
                pos_++;
                p->children.push_back(std::move(e));
                e = std::move(p);
            } else {
                break;
            }
        }
        return e;
    }

    ExprPtr parsePrimary() {
        if (!at()) {
            fail("expected an expression");
            return make(Expr::Kind::Literal, "0", line());
        }
        const Token& t = t_[pos_];
        if (t.kind == TokenKind::Int || t.kind == TokenKind::Uint || t.kind == TokenKind::Float) {
            auto e = make(Expr::Kind::Literal, t.text, t.line);
            e->literal = t.kind;
            pos_++;
            return e;
        }
        if (peekIs("(")) {
            auto e = make(Expr::Kind::Paren, "(", t.line);
            pos_++;
            e->children.push_back(parseExpression());
            expect(")");
            return e;
        }
        if (t.kind == TokenKind::Identifier) {
            if (t.text == "true" || t.text == "false") {
                auto e = make(Expr::Kind::Literal, t.text, t.line);
                e->literal = TokenKind::Identifier;  // bool
                pos_++;
                return e;
            }
            // type[N](...) array constructor
            if (isTypeName() && peekIs("[", 1)) {
                auto call = make(Expr::Kind::Call, t.text, t.line);
                call->arrayConstructor = true;
                pos_ += 2;
                if (!peekIs("]")) call->arraySize = parseAssignment();
                expect("]");
                parseArguments(*call);
                return call;
            }
            if (peekIs("(", 1)) {
                auto call = make(Expr::Kind::Call, t.text, t.line);
                pos_++;
                parseArguments(*call);
                return call;
            }
            pos_++;
            return make(Expr::Kind::Identifier, t.text, t.line);
        }
        fail("unexpected token");
        return make(Expr::Kind::Literal, "0", line());
    }

    void parseArguments(Expr& call) {
        expect("(");
        if (peekIs("void") && peekIs(")", 1)) pos_++;
        while (!failed_ && at() && !peekIs(")")) {
            call.children.push_back(parseAssignment());
            if (!peekIs(",")) break;
            pos_++;
        }
        expect(")");
    }

    const std::vector<Token>& t_;
    size_t pos_ = 0;
    bool failed_ = false;
    std::string error_;
    std::set<std::string> structs_;
};

// --- printer -----------------------------------------------------------------

std::string printType(const TypeSpec& t);
void printDeclaration(const Declaration& d, std::string& out);

std::string printStruct(const StructDef& s) {
    std::string out = "struct " + s.name + " {\n";
    for (const auto& m : s.members) {
        out += "    ";
        printDeclaration(m, out);
    }
    return out + "}";
}

std::string printType(const TypeSpec& t) {
    std::string out = t.structDef ? printStruct(*t.structDef) : t.name;
    if (t.hasArray) out += "[" + (t.arraySize ? printExpr(*t.arraySize) : std::string()) + "]";
    return out;
}

void printDeclaration(const Declaration& d, std::string& out) {
    for (const auto& q : d.qualifiers) out += q + " ";
    out += printType(d.type);
    for (size_t i = 0; i < d.vars.size(); i++) {
        const Declarator& v = d.vars[i];
        out += (i == 0 ? " " : ", ") + v.name;
        if (v.hasArray) out += "[" + (v.arraySize ? printExpr(*v.arraySize) : std::string()) + "]";
        if (v.init) out += " = " + printExpr(*v.init);
    }
    out += ";\n";
}

void printStmt(const Stmt& s, int indent, std::string& out) {
    std::string pad(size_t(indent) * 4, ' ');
    switch (s.kind) {
        case Stmt::Kind::Block:
            out += pad + "{\n";
            for (const auto& b : s.body) printStmt(*b, indent + 1, out);
            out += pad + "}\n";
            break;
        case Stmt::Kind::Declaration:
            out += pad;
            printDeclaration(*s.decl, out);
            break;
        case Stmt::Kind::Expression:
            out += pad + printExpr(*s.expr) + ";\n";
            break;
        case Stmt::Kind::If:
            out += pad + "if (" + printExpr(*s.expr) + ")\n";
            printStmt(*s.body[0], indent + 1, out);
            if (s.body.size() > 1) {
                out += pad + "else\n";
                printStmt(*s.body[1], indent + 1, out);
            }
            break;
        case Stmt::Kind::For: {
            std::string init;
            if (s.init && s.init->kind == Stmt::Kind::Declaration) {
                printDeclaration(*s.init->decl, init);
                init.pop_back();  // newline
            } else if (s.init && s.init->expr) {
                init = printExpr(*s.init->expr) + ";";
            } else {
                init = ";";
            }
            out += pad + "for (" + init + " " + (s.expr ? printExpr(*s.expr) : "") + "; " +
                   (s.step ? printExpr(*s.step) : "") + ")\n";
            printStmt(*s.body[0], indent + 1, out);
            break;
        }
        case Stmt::Kind::While:
            out += pad + "while (" + printExpr(*s.expr) + ")\n";
            printStmt(*s.body[0], indent + 1, out);
            break;
        case Stmt::Kind::DoWhile:
            out += pad + "do\n";
            printStmt(*s.body[0], indent + 1, out);
            out += pad + "while (" + printExpr(*s.expr) + ");\n";
            break;
        case Stmt::Kind::Return:
            out += pad + "return" + (s.expr ? " " + printExpr(*s.expr) : "") + ";\n";
            break;
        case Stmt::Kind::Break: out += pad + "break;\n"; break;
        case Stmt::Kind::Continue: out += pad + "continue;\n"; break;
        case Stmt::Kind::Discard: out += pad + "discard;\n"; break;
        case Stmt::Kind::Empty: out += pad + ";\n"; break;
    }
}

}  // namespace

std::optional<TranslationUnit> parseGlsl(const std::vector<Token>& tokens, std::string& error) {
    return Parser(tokens).run(error);
}

std::string printExpr(const Expr& e) {
    auto child = [&](size_t i) { return printExpr(*e.children[i]); };
    switch (e.kind) {
        case Expr::Kind::Literal:
        case Expr::Kind::Identifier: return e.text;
        case Expr::Kind::Paren: return "(" + child(0) + ")";
        case Expr::Kind::Unary: {
            // "-" before "-1.3" (a macro holding a negative number) must not
            // print as the decrement operator "--".
            std::string operand = child(0);
            bool merge = !e.text.empty() && !operand.empty() &&
                         (e.text.back() == '-' || e.text.back() == '+') &&
                         operand.front() == e.text.back();
            return e.text + (merge ? " " : "") + operand;
        }
        case Expr::Kind::Postfix: return child(0) + e.text;
        case Expr::Kind::Binary:
        case Expr::Kind::Assign: return child(0) + " " + e.text + " " + child(1);
        case Expr::Kind::Ternary: return child(0) + " ? " + child(1) + " : " + child(2);
        case Expr::Kind::Index: return child(0) + "[" + child(1) + "]";
        case Expr::Kind::Member: return child(0) + "." + e.text;
        case Expr::Kind::Comma: {
            std::string s;
            for (size_t i = 0; i < e.children.size(); i++) s += (i ? ", " : "") + child(i);
            return s;
        }
        case Expr::Kind::Fmod: {
            std::string a = "(" + child(0) + ")", b = "(" + child(1) + ")";
            return "(" + a + " - " + b + " * trunc(" + a + " / " + b + "))";
        }
        case Expr::Kind::Call: {
            std::string s = e.text;
            if (e.arrayConstructor) s += "[" + (e.arraySize ? printExpr(*e.arraySize) : "") + "]";
            s += "(";
            for (size_t i = 0; i < e.children.size(); i++) s += (i ? ", " : "") + child(i);
            return s + ")";
        }
    }
    return "";
}

std::string printGlsl(const TranslationUnit& unit) {
    std::string out;
    for (const auto& item : unit.items) {
        switch (item.kind) {
            case TopLevel::Kind::Precision: out += item.text + "\n"; break;
            case TopLevel::Kind::Declaration: printDeclaration(*item.decl, out); break;
            case TopLevel::Kind::Function: {
                const Function& f = *item.function;
                for (const auto& q : f.qualifiers) out += q + " ";
                out += printType(f.returnType) + " " + f.name + "(";
                for (size_t i = 0; i < f.params.size(); i++) {
                    const Parameter& p = f.params[i];
                    if (i) out += ", ";
                    for (const auto& q : p.qualifiers) out += q + " ";
                    out += printType(p.type);
                    if (!p.name.empty()) out += " " + p.name;
                    if (p.hasArray)
                        out += "[" + (p.arraySize ? printExpr(*p.arraySize) : std::string()) + "]";
                }
                out += ")";
                if (!f.body) {
                    out += ";\n";
                } else {
                    out += "\n";
                    printStmt(*f.body, 0, out);
                }
                break;
            }
        }
    }
    return out;
}

}  // namespace canvas::scene
