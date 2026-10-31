// Implementation of the HLSL conversion pass declared in Conversions.h.
#include "scene/shader/Conversions.h"

#include <algorithm>
#include <climits>
#include <set>
#include <cstring>

namespace canvas::scene {

using Base = GlslType::Base;

namespace {

// --- built in functions -------------------------------------------------------
//
// Parameter and return patterns: genF/genI/genU/genB are a scalar or
// vector of one size per call (float, int, uint, bool); vecF/vecI/vecU/vecB
// the same but at least two components (relational functions); "same"
// returns the first argument's type (matrix functions); other names are
// concrete types.
struct BuiltinSig {
    const char* ret;
    std::vector<const char*> params;
};

const std::map<std::string, std::vector<BuiltinSig>>& builtins() {
    static const std::map<std::string, std::vector<BuiltinSig>> table = [] {
        std::map<std::string, std::vector<BuiltinSig>> t;
        auto add = [&](const char* name, const char* ret, std::vector<const char*> params) {
            t[name].push_back({ret, std::move(params)});
        };
        for (const char* f : {"radians", "degrees", "sin", "cos", "tan", "asin", "acos", "atan",
                              "sinh", "cosh", "tanh", "asinh", "acosh", "atanh", "exp", "log",
                              "exp2", "log2", "sqrt", "inversesqrt", "floor", "trunc", "round",
                              "roundEven", "ceil", "fract", "normalize", "dFdx", "dFdy", "fwidth"})
            add(f, "genF", {"genF"});
        add("abs", "genF", {"genF"});
        add("abs", "genI", {"genI"});
        add("sign", "genF", {"genF"});
        add("sign", "genI", {"genI"});
        add("atan", "genF", {"genF", "genF"});
        add("pow", "genF", {"genF", "genF"});
        add("mod", "genF", {"genF", "float"});
        add("mod", "genF", {"genF", "genF"});
        for (const char* f : {"min", "max"}) {
            add(f, "genF", {"genF", "genF"});
            add(f, "genF", {"genF", "float"});
            add(f, "genI", {"genI", "genI"});
            add(f, "genI", {"genI", "int"});
            add(f, "genU", {"genU", "genU"});
            add(f, "genU", {"genU", "uint"});
        }
        add("clamp", "genF", {"genF", "genF", "genF"});
        add("clamp", "genF", {"genF", "float", "float"});
        add("clamp", "genI", {"genI", "genI", "genI"});
        add("clamp", "genI", {"genI", "int", "int"});
        add("clamp", "genU", {"genU", "genU", "genU"});
        add("clamp", "genU", {"genU", "uint", "uint"});
        add("mix", "genF", {"genF", "genF", "genF"});
        add("mix", "genF", {"genF", "genF", "float"});
        add("mix", "genF", {"genF", "genF", "genB"});
        add("step", "genF", {"genF", "genF"});
        add("step", "genF", {"float", "genF"});
        add("smoothstep", "genF", {"genF", "genF", "genF"});
        add("smoothstep", "genF", {"float", "float", "genF"});
        add("isnan", "genB", {"genF"});
        add("isinf", "genB", {"genF"});
        add("floatBitsToInt", "genI", {"genF"});
        add("floatBitsToUint", "genU", {"genF"});
        add("intBitsToFloat", "genF", {"genI"});
        add("uintBitsToFloat", "genF", {"genU"});
        add("length", "float", {"genF"});
        add("distance", "float", {"genF", "genF"});
        add("dot", "float", {"genF", "genF"});
        add("cross", "vec3", {"vec3", "vec3"});
        add("faceforward", "genF", {"genF", "genF", "genF"});
        add("reflect", "genF", {"genF", "genF"});
        add("refract", "genF", {"genF", "genF", "float"});
        for (const char* f : {"lessThan", "lessThanEqual", "greaterThan", "greaterThanEqual"}) {
            add(f, "vecB", {"vecF", "vecF"});
            add(f, "vecB", {"vecI", "vecI"});
            add(f, "vecB", {"vecU", "vecU"});
        }
        for (const char* f : {"equal", "notEqual"}) {
            add(f, "vecB", {"vecF", "vecF"});
            add(f, "vecB", {"vecI", "vecI"});
            add(f, "vecB", {"vecU", "vecU"});
            add(f, "vecB", {"vecB", "vecB"});
        }
        add("any", "bool", {"vecB"});
        add("all", "bool", {"vecB"});
        add("not", "vecB", {"vecB"});
        add("texture", "vec4", {"sampler2D", "vec2"});
        add("texture", "vec4", {"sampler2D", "vec2", "float"});
        add("texture", "vec4", {"sampler3D", "vec3"});
        add("texture", "vec4", {"samplerCube", "vec3"});
        add("texture", "float", {"sampler2DShadow", "vec3"});
        add("texture", "vec4", {"sampler2DArray", "vec3"});
        add("textureLod", "vec4", {"sampler2D", "vec2", "float"});
        add("textureLod", "vec4", {"sampler3D", "vec3", "float"});
        add("textureLod", "vec4", {"samplerCube", "vec3", "float"});
        add("textureGrad", "vec4", {"sampler2D", "vec2", "vec2", "vec2"});
        add("textureProj", "vec4", {"sampler2D", "vec3"});
        add("textureProj", "vec4", {"sampler2D", "vec4"});
        add("textureSize", "ivec2", {"sampler2D", "int"});
        add("textureSize", "ivec3", {"sampler3D", "int"});
        add("texelFetch", "vec4", {"sampler2D", "ivec2", "int"});
        add("texelFetch", "vec4", {"sampler3D", "ivec3", "int"});
        add("transpose", "transpose", {"mat"});
        add("determinant", "float", {"mat"});
        add("inverse", "same", {"mat"});
        add("matrixCompMult", "same", {"mat", "mat"});
        return t;
    }();
    return table;
}

bool isGeneric(const std::string& p) {
    return p == "genF" || p == "genI" || p == "genU" || p == "genB" || p == "vecF" ||
           p == "vecI" || p == "vecU" || p == "vecB";
}

Base genericBase(const std::string& p) {
    char c = p.back();
    return c == 'F' ? Base::Float : c == 'I' ? Base::Int : c == 'U' ? Base::Uint : Base::Bool;
}

int rank(Base b) {
    switch (b) {
        case Base::Bool: return 0;
        case Base::Int: return 1;
        case Base::Uint: return 2;
        case Base::Float: return 3;
        default: return -1;
    }
}

// GLSL 3.30 accepts this without a constructor (int and uint to float).
bool implicitInGlsl(const GlslType& from, const GlslType& to) {
    if (from == to) return true;
    if (!from.numeric() || !to.numeric() || from.array || to.array) return false;
    if (from.size != to.size || from.columns != to.columns) return false;
    return to.base == Base::Float && (from.base == Base::Int || from.base == Base::Uint);
}

// HLSL converts this implicitly: numeric conversion then splat or
// truncation [HLSL spec].
bool convertibleInHlsl(const GlslType& from, const GlslType& to) {
    if (from == to) return true;
    if (!from.numeric() || !to.numeric() || from.array || to.array) return false;
    if (from.isMatrix() || to.isMatrix()) {
        if (from.isMatrix() && to.isMatrix())
            return from.columns >= to.columns && from.size >= to.size;
        return from.isScalar();  // scalar splat into a matrix
    }
    return from.size == 1 || to.size <= from.size;
}

int conversionCost(const GlslType& from, const GlslType& to) {
    if (from == to) return 0;
    if (implicitInGlsl(from, to)) return 1;
    return 2;
}

class Checker {
public:
    Checker(const std::map<std::string, GlslType>& inputs,
            const std::map<std::string, GlslType>& outputs)
        : inputs_(inputs), outputs_(outputs) {}

    void run(TranslationUnit& unit) {
        scopes_.emplace_back();
        for (const char* v : {"gl_Position", "gl_FragCoord"}) declare(v, GlslType::vector(Base::Float, 4));
        declare("gl_FragDepth", GlslType::scalar(Base::Float));
        declare("gl_PointSize", GlslType::scalar(Base::Float));
        declare("gl_VertexID", GlslType::scalar(Base::Int));
        declare("gl_InstanceID", GlslType::scalar(Base::Int));
        declare("gl_FrontFacing", GlslType::scalar(Base::Bool));
        declare("gl_PointCoord", GlslType::vector(Base::Float, 2));
        for (auto& item : unit.items) {
            if (item.kind == TopLevel::Kind::Declaration) {
                widenInput(*item.decl);
                widenOutput(*item.decl);
                declaration(*item.decl, false);
            } else if (item.kind == TopLevel::Kind::Function) {
                function(*item.function);
            }
        }
    }

    size_t conversions = 0;
    std::map<std::string, size_t> counts;

private:
    // --- types and names ---------------------------------------------------
    GlslType typeOf(const TypeSpec& spec) {
        if (spec.structDef) defineStruct(*spec.structDef);
        GlslType t = builtinType(spec.name);
        if (!t.known() && structs_.count(spec.name)) {
            t.base = Base::Struct;
            t.name = spec.name;
        }
        if (spec.hasArray) t.array = arrayLength(spec.arraySize.get());
        return t;
    }

    static int arrayLength(const Expr* size) {
        if (size && size->kind == Expr::Kind::Literal && size->literal == TokenKind::Int) {
            try {
                return std::stoi(size->text, nullptr, 0);
            } catch (...) {
            }
        }
        return -1;
    }

    void defineStruct(const StructDef& s) {
        std::vector<std::pair<std::string, GlslType>> fields;
        for (const auto& m : s.members) {
            GlslType t = typeOf(m.type);
            for (const auto& v : m.vars) {
                GlslType f = t;
                if (v.hasArray) f.array = arrayLength(v.arraySize.get());
                fields.emplace_back(v.name, f);
            }
        }
        if (!s.name.empty()) structs_[s.name] = std::move(fields);
    }

    void declare(const std::string& name, const GlslType& t, bool constant = false) {
        scopes_.back()[name] = t;
        if (constant) constants_.insert(name + "@" + std::to_string(scopes_.size()));
        else constants_.erase(name + "@" + std::to_string(scopes_.size()));
    }

    bool isConstantName(const std::string& name) const {
        for (size_t depth = scopes_.size(); depth > 0; depth--) {
            if (scopes_[depth - 1].count(name))
                return constants_.count(name + "@" + std::to_string(depth)) > 0;
        }
        return false;
    }

    // A GLSL constant expression: literals, const names, operators and
    // constructors or math built ins over them (no texture or derivative
    // functions, no user functions).
    bool isConstant(const Expr& e) const {
        switch (e.kind) {
            case Expr::Kind::Literal: return true;
            case Expr::Kind::Identifier: return isConstantName(e.text);
            case Expr::Kind::Call: {
                bool ctor = builtinType(e.text).known() && builtinType(e.text).base != Base::Sampler;
                bool math = builtins().count(e.text) && e.text.rfind("texture", 0) != 0 &&
                            e.text.rfind("texel", 0) != 0 && e.text != "dFdx" && e.text != "dFdy" &&
                            e.text != "fwidth";
                if (!ctor && !math) return false;
                break;
            }
            default: break;
        }
        for (const auto& c : e.children)
            if (!isConstant(*c)) return false;
        return true;
    }

    GlslType lookup(const std::string& name) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it)
            if (auto v = it->find(name); v != it->end()) return v->second;
        return {};
    }

    void widenOutput(Declaration& d) {
        bool output = std::find(d.qualifiers.begin(), d.qualifiers.end(), "out") != d.qualifiers.end();
        if (!output || d.vars.size() != 1) return;
        auto it = outputs_.find(d.vars[0].name);
        if (it == outputs_.end()) return;
        d.type.name = it->second.glsl();
        padded_[d.vars[0].name] = it->second;
        counts["output widened to the fragment input"]++;
        conversions++;
    }

    void widenInput(Declaration& d) {
        bool input = std::find(d.qualifiers.begin(), d.qualifiers.end(), "in") != d.qualifiers.end();
        if (!input || d.vars.size() != 1) return;
        auto it = inputs_.find(d.vars[0].name);
        if (it == inputs_.end()) return;
        d.type.name = it->second.glsl();
        counts["input widened to the vertex output"]++;
        conversions++;
    }

    // --- conversions -----------------------------------------------------------
    void convert(ExprPtr& e, const GlslType& target, const char* why) {
        if (!e || !e->type.known() || !target.known() || target.base == Base::Void) return;
        if (implicitInGlsl(e->type, target) || !convertibleInHlsl(e->type, target)) return;
        auto call = std::make_unique<Expr>();
        call->kind = Expr::Kind::Call;
        call->text = target.glsl();
        call->line = e->line;
        call->type = target;
        call->children.push_back(std::move(e));
        e = std::move(call);
        conversions++;
        counts[why]++;
    }

    static const char* reasonFor(const GlslType& from, const GlslType& to) {
        if (from.size == 1 && to.size > 1) return "splat";
        if (from.size > to.size || (from.isMatrix() && to.isMatrix())) return "truncation";
        if (to.base == Base::Bool) return "to bool";
        return "numeric";
    }

    void convertTo(ExprPtr& e, const GlslType& target) {
        if (e && e->type.known()) convert(e, target, reasonFor(e->type, target));
    }

    void toBool(ExprPtr& e) {
        if (e && e->type.isScalar() && e->type.base != Base::Bool)
            convert(e, GlslType::scalar(Base::Bool), "condition to bool");
    }

    // Brings two operands to a common type; returns it (Unknown if none).
    GlslType unify(ExprPtr& a, ExprPtr& b, bool allowScalarWithVector) {
        const GlslType& x = a->type;
        const GlslType& y = b->type;
        if (!x.isScalarOrVector() || !y.isScalarOrVector()) return {};
        Base base = rank(x.base) >= rank(y.base) ? x.base : y.base;
        int size;
        if (x.size == 1 || y.size == 1) size = std::max(x.size, y.size);
        else size = std::min(x.size, y.size);
        GlslType common = GlslType::vector(base, size);
        auto shaped = [&](const GlslType& from) {
            // Keep a scalar scalar where GLSL mixes scalars with vectors.
            if (allowScalarWithVector && from.size == 1) return GlslType::scalar(base);
            return common;
        };
        convertTo(a, shaped(x));
        convertTo(b, shaped(y));
        return common;
    }

    // --- declarations, functions, statements -------------------------------
    void declaration(Declaration& d, bool local) {
        GlslType base = typeOf(d.type);
        auto constQualifier = std::find(d.qualifiers.begin(), d.qualifiers.end(), "const");
        bool constant = constQualifier != d.qualifiers.end();
        for (auto& v : d.vars) {
            GlslType t = base;
            if (v.hasArray) t.array = arrayLength(v.arraySize.get());
            if (v.arraySize) check(v.arraySize);
            if (v.init) {
                check(v.init);
                convertTo(v.init, t);
                // HLSL const only forbids writes; GLSL wants a constant
                // initializer, so a local loses const instead.
                if (constant && local && !isConstant(*v.init)) {
                    d.qualifiers.erase(constQualifier);
                    constQualifier = d.qualifiers.end();
                    constant = false;
                    conversions++;
                    counts["const dropped from a local with a non constant initializer"]++;
                }
            }
            declare(v.name, t, constant);
        }
    }

    void function(Function& f) {
        Signature sig;
        sig.ret = typeOf(f.returnType);
        for (const auto& p : f.params) {
            GlslType t = typeOf(p.type);
            if (p.hasArray) t.array = arrayLength(p.arraySize.get());
            sig.params.push_back(t);
            bool output = false;
            for (const auto& q : p.qualifiers) output |= q == "out" || q == "inout";
            sig.output.push_back(output);
        }
        auto& list = functions_[f.name];
        bool known = false;
        for (const auto& s : list) known |= s.params == sig.params;
        if (!known) list.push_back(sig);
        if (!f.body) return;
        scopes_.emplace_back();
        for (size_t i = 0; i < f.params.size(); i++)
            if (!f.params[i].name.empty()) declare(f.params[i].name, sig.params[i]);
        GlslType saved = returnType_;
        returnType_ = sig.ret;
        statement(*f.body);
        returnType_ = saved;
        scopes_.pop_back();
    }

    void statement(Stmt& s) {
        switch (s.kind) {
            case Stmt::Kind::Block:
                scopes_.emplace_back();
                for (auto& b : s.body) statement(*b);
                scopes_.pop_back();
                break;
            case Stmt::Kind::Declaration:
                declaration(*s.decl, true);
                break;
            case Stmt::Kind::Expression:
                check(s.expr);
                break;
            case Stmt::Kind::If:
                check(s.expr);
                toBool(s.expr);
                for (auto& b : s.body) statement(*b);
                break;
            case Stmt::Kind::For:
                scopes_.emplace_back();
                if (s.init) statement(*s.init);
                if (s.expr) {
                    check(s.expr);
                    toBool(s.expr);
                }
                if (s.step) check(s.step);
                statement(*s.body[0]);
                scopes_.pop_back();
                break;
            case Stmt::Kind::While:
            case Stmt::Kind::DoWhile:
                check(s.expr);
                toBool(s.expr);
                statement(*s.body[0]);
                break;
            case Stmt::Kind::Return:
                if (s.expr) {
                    check(s.expr);
                    convertTo(s.expr, returnType_);
                }
                break;
            default:
                break;
        }
    }

    // --- expressions -------------------------------------------------------------
    static ExprPtr clone(const Expr& e) {
        auto c = std::make_unique<Expr>();
        c->kind = e.kind;
        c->text = e.text;
        c->literal = e.literal;
        c->arrayConstructor = e.arrayConstructor;
        if (e.arraySize) c->arraySize = clone(*e.arraySize);
        c->type = e.type;
        c->line = e.line;
        for (const auto& ch : e.children) c->children.push_back(clone(*ch));
        return c;
    }

    void check(ExprPtr& e) {
        if (!e) return;
        Expr& x = *e;
        switch (x.kind) {
            case Expr::Kind::Literal:
                x.type = x.literal == TokenKind::Int     ? GlslType::scalar(Base::Int)
                         : x.literal == TokenKind::Uint  ? GlslType::scalar(Base::Uint)
                         : x.literal == TokenKind::Float ? GlslType::scalar(Base::Float)
                                                         : GlslType::scalar(Base::Bool);
                return;
            case Expr::Kind::Identifier:
                x.type = lookup(x.text);
                return;
            case Expr::Kind::Paren:
                check(x.children[0]);
                x.type = x.children[0]->type;
                return;
            case Expr::Kind::Unary:
                check(x.children[0]);
                if (x.text == "!") {
                    toBool(x.children[0]);
                    x.type = GlslType::scalar(Base::Bool);
                } else {
                    x.type = x.children[0]->type;
                }
                return;
            case Expr::Kind::Postfix:
                check(x.children[0]);
                x.type = x.children[0]->type;
                return;
            case Expr::Kind::Comma:
                for (auto& c : x.children) check(c);
                x.type = x.children.back()->type;
                return;
            case Expr::Kind::Ternary:
                for (auto& c : x.children) check(c);
                toBool(x.children[0]);
                if (x.children[1]->type == x.children[2]->type) x.type = x.children[1]->type;
                else x.type = unify(x.children[1], x.children[2], false);
                return;
            case Expr::Kind::Binary:
                binary(e);
                return;
            case Expr::Kind::Assign:
                assign(e);
                return;
            case Expr::Kind::Index: {
                check(x.children[0]);
                check(x.children[1]);
                const GlslType& idx = x.children[1]->type;
                if (idx.isScalar() && (idx.base == Base::Float || idx.base == Base::Bool))
                    convert(x.children[1], GlslType::scalar(Base::Int), "array index to int");
                const GlslType& t = x.children[0]->type;
                if (t.array != 0) x.type = t.element();
                else if (t.isMatrix()) x.type = GlslType::vector(Base::Float, t.size);
                else if (t.isVector()) x.type = GlslType::scalar(t.base);
                else x.type = {};
                return;
            }
            case Expr::Kind::Member:
                member(e);
                return;
            case Expr::Kind::Call:
                call(e);
                return;
            case Expr::Kind::Fmod:
                return;
        }
    }

    void binary(ExprPtr& e) {
        Expr& x = *e;
        check(x.children[0]);
        check(x.children[1]);
        const std::string& op = x.text;
        const GlslType a = x.children[0]->type;
        const GlslType b = x.children[1]->type;
        x.type = {};
        if (!a.known() || !b.known()) return;
        if (op == "&&" || op == "||" || op == "^^") {
            toBool(x.children[0]);
            toBool(x.children[1]);
            x.type = GlslType::scalar(Base::Bool);
            return;
        }
        if (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=") {
            if (a != b && a.isScalarOrVector() && b.isScalarOrVector()) unify(x.children[0], x.children[1], false);
            x.type = GlslType::scalar(Base::Bool);
            return;
        }
        if (a.isMatrix() || b.isMatrix()) {
            x.type = matrixArithmetic(op, a, b);
            return;
        }
        if (!a.isScalarOrVector() || !b.isScalarOrVector()) return;
        if (op == "%" && (a.base == Base::Float || b.base == Base::Float)) {
            GlslType common = unify(x.children[0], x.children[1], true);
            auto f = std::make_unique<Expr>();
            f->kind = Expr::Kind::Fmod;
            f->line = x.line;
            f->type = common;
            f->children = std::move(x.children);
            e = std::move(f);
            conversions++;
            counts["% on floats as fmod"]++;
            return;
        }
        if (op == "<<" || op == ">>") {
            x.type = a;
            return;
        }
        // Arithmetic and bitwise: a common base; vectors of different sizes
        // truncate to the smaller; scalars mix with vectors as in GLSL.
        if (a.base == b.base && (a.size == b.size || a.size == 1 || b.size == 1)) {
            x.type = GlslType::vector(a.base, std::max(a.size, b.size));
            return;
        }
        x.type = unify(x.children[0], x.children[1], true);
    }

    static GlslType matrixArithmetic(const std::string& op, const GlslType& a, const GlslType& b) {
        if (op != "*") {
            if (a == b) return a;
            if (a.isMatrix() && b.isScalar()) return a;
            if (b.isMatrix() && a.isScalar()) return b;
            return {};
        }
        if (a.isMatrix() && b.isMatrix()) {
            if (a.columns != b.size) return {};
            GlslType t = GlslType::scalar(Base::Float);
            t.columns = b.columns;
            t.size = a.size;
            return t;
        }
        if (a.isMatrix() && b.isVector()) return a.columns == b.size ? GlslType::vector(Base::Float, a.size) : GlslType{};
        if (a.isVector() && b.isMatrix()) return a.size == b.size ? GlslType::vector(Base::Float, b.columns) : GlslType{};
        if (a.isMatrix() && b.isScalar()) return a;
        if (b.isMatrix() && a.isScalar()) return b;
        return {};
    }

    void assign(ExprPtr& e) {
        Expr& x = *e;
        check(x.children[0]);
        check(x.children[1]);
        const GlslType target = x.children[0]->type;
        x.type = target;
        if (!target.known() || !x.children[1]->type.known()) return;
        if (x.text == "=") {
            // A vertex output widened for the fragment stage: pad with zeros.
            const GlslType& value = x.children[1]->type;
            if (x.children[0]->kind == Expr::Kind::Identifier && padded_.count(x.children[0]->text) &&
                value.isScalarOrVector() && value.base == target.base && value.size < target.size) {
                auto call = std::make_unique<Expr>();
                call->kind = Expr::Kind::Call;
                call->text = target.glsl();
                call->type = target;
                call->line = x.line;
                call->children.push_back(std::move(x.children[1]));
                for (int k = value.size; k < target.size; k++) {
                    auto zero = std::make_unique<Expr>();
                    zero->kind = Expr::Kind::Literal;
                    zero->literal = target.base == Base::Float ? TokenKind::Float : TokenKind::Int;
                    zero->text = target.base == Base::Float ? "0.0" : "0";
                    zero->type = GlslType::scalar(target.base);
                    call->children.push_back(std::move(zero));
                }
                x.children[1] = std::move(call);
                conversions++;
                counts["output padded with zeros"]++;
                return;
            }
            convertTo(x.children[1], target);
            return;
        }
        if (x.text == "%=" && (target.base == Base::Float || x.children[1]->type.base == Base::Float)) {
            // a %= b  ->  a = fmod(a, b)
            auto f = std::make_unique<Expr>();
            f->kind = Expr::Kind::Fmod;
            f->line = x.line;
            f->type = target;
            f->children.push_back(clone(*x.children[0]));
            f->children.push_back(std::move(x.children[1]));
            GlslType common = unify(f->children[0], f->children[1], true);
            (void)common;
            x.children[1] = std::move(f);
            x.text = "=";
            convertTo(x.children[1], target);
            conversions++;
            counts["% on floats as fmod"]++;
            return;
        }
        const GlslType& value = x.children[1]->type;
        if (target.isMatrix() || value.isMatrix()) return;
        if (!target.isScalarOrVector() || !value.isScalarOrVector()) return;
        // A scalar works on any vector; otherwise the value takes the
        // target's shape and base.
        if (value.size == 1 && target.size > 1 && (implicitInGlsl(GlslType::scalar(value.base), GlslType::scalar(target.base)) || value.base == target.base))
            return;
        if (value.size == 1 && target.size > 1) {
            convertTo(x.children[1], GlslType::scalar(target.base));
            return;
        }
        convertTo(x.children[1], target);
    }

    void member(ExprPtr& e) {
        Expr& x = *e;
        check(x.children[0]);
        const GlslType t = x.children[0]->type;
        x.type = {};
        if (t.base == Base::Struct && t.array == 0) {
            auto s = structs_.find(t.name);
            if (s == structs_.end()) return;
            for (const auto& [name, type] : s->second)
                if (name == x.text) x.type = type;
            return;
        }
        if (!t.isScalarOrVector()) return;
        const std::string& sw = x.text;
        static const char* sets[] = {"xyzw", "rgba", "stpq"};
        int length = int(sw.size());
        if (length < 1 || length > 4) return;
        for (const char* set : sets) {
            bool ok = true;
            for (char c : sw) {
                const char* p = std::strchr(set, c);
                ok &= p != nullptr && *p != '\0' && int(p - set) < t.size;
            }
            if (!ok) continue;
            GlslType result = length == 1 ? GlslType::scalar(t.base) : GlslType::vector(t.base, length);
            if (t.size == 1) {
                // HLSL swizzles scalars; GLSL 3.30 does not.
                ExprPtr inner = std::move(x.children[0]);
                if (length == 1) {
                    e = std::move(inner);
                } else {
                    auto call = std::make_unique<Expr>();
                    call->kind = Expr::Kind::Call;
                    call->text = result.glsl();
                    call->line = inner->line;
                    call->type = result;
                    call->children.push_back(std::move(inner));
                    e = std::move(call);
                }
                conversions++;
                counts["swizzle of a scalar"]++;
                return;
            }
            x.type = result;
            return;
        }
    }

    struct Signature {
        GlslType ret;
        std::vector<GlslType> params;
        std::vector<bool> output;
    };

    void call(ExprPtr& e) {
        Expr& x = *e;
        for (auto& c : x.children) check(c);
        if (x.arraySize) check(x.arraySize);
        x.type = {};
        // Constructors.
        GlslType ctor = builtinType(x.text);
        if (ctor.known() && ctor.base != Base::Sampler) {
            x.type = ctor;
            if (x.arrayConstructor) x.type.array = x.arraySize ? arrayLength(x.arraySize.get()) : int(x.children.size());
            return;
        }
        if (auto s = structs_.find(x.text); s != structs_.end()) {
            x.type.base = Base::Struct;
            x.type.name = x.text;
            if (s->second.size() == x.children.size())
                for (size_t i = 0; i < x.children.size(); i++) convertTo(x.children[i], s->second[i].second);
            return;
        }
        for (const auto& c : x.children)
            if (!c->type.known()) return;  // cannot resolve with unknown arguments
        if (auto f = functions_.find(x.text); f != functions_.end()) {
            userCall(x, f->second);
            return;
        }
        if (auto b = builtins().find(x.text); b != builtins().end()) builtinCall(x, b->second);
    }

    void userCall(Expr& x, const std::vector<Signature>& candidates) {
        const Signature* best = nullptr;
        int bestCost = INT_MAX;
        for (const auto& s : candidates) {
            if (s.params.size() != x.children.size()) continue;
            int cost = 0;
            bool viable = true;
            for (size_t i = 0; i < s.params.size() && viable; i++) {
                const GlslType& from = x.children[i]->type;
                if (s.output[i]) viable = from == s.params[i];
                else viable = convertibleInHlsl(from, s.params[i]) || implicitInGlsl(from, s.params[i]);
                cost += viable ? conversionCost(from, s.params[i]) : 0;
            }
            if (viable && cost < bestCost) {
                best = &s;
                bestCost = cost;
            }
        }
        if (!best) return;
        for (size_t i = 0; i < best->params.size(); i++)
            if (!best->output[i]) convertTo(x.children[i], best->params[i]);
        x.type = best->ret;
    }

    // The type a pattern stands for, given the call's generic size.
    static GlslType patternType(const std::string& p, int size, const std::vector<ExprPtr>& args) {
        if (isGeneric(p)) return GlslType::vector(genericBase(p), size);
        if (p == "same") return args.empty() ? GlslType{} : args[0]->type;
        if (p == "transpose") {
            if (args.empty() || !args[0]->type.isMatrix()) return {};
            GlslType t = args[0]->type;
            std::swap(t.columns, t.size);
            return t;
        }
        if (p == "mat") return args.empty() ? GlslType{} : args[0]->type;
        return builtinType(p);
    }

    void builtinCall(Expr& x, const std::vector<BuiltinSig>& candidates) {
        const size_t n = x.children.size();
        auto genericSize = [&](const BuiltinSig& s, bool hlsl) -> int {
            int size = 0;
            for (size_t i = 0; i < n; i++) {
                if (!isGeneric(s.params[i])) continue;
                int sz = x.children[i]->type.isScalarOrVector() ? x.children[i]->type.size : -1;
                if (sz < 0) return -1;
                if (!hlsl) {
                    if (size == 0) size = sz;
                    else if (size != sz) return -1;
                } else if (sz > 1) {
                    size = size == 0 ? sz : std::min(size, sz);
                }
            }
            return size == 0 ? 1 : size;
        };
        // 1. As written (GLSL's own implicit conversions only).
        for (const auto& s : candidates) {
            if (s.params.size() != n) continue;
            int size = genericSize(s, false);
            if (size < 0) continue;
            bool ok = true;
            for (size_t i = 0; i < n && ok; i++) {
                GlslType want = patternType(s.params[i], size, x.children);
                std::string p = s.params[i];
                if (p.rfind("vec", 0) == 0 && isGeneric(p) && size < 2) ok = false;
                else if (p == "mat") ok = x.children[i]->type.isMatrix();
                else ok = want.known() && implicitInGlsl(x.children[i]->type, want);
            }
            if (ok) {
                x.type = patternType(s.ret, size, x.children);
                return;
            }
        }
        // 2. The HLSL way: smallest vector size, float when any argument is.
        const BuiltinSig* best = nullptr;
        int bestCost = INT_MAX, bestSize = 1;
        for (const auto& s : candidates) {
            if (s.params.size() != n) continue;
            int size = genericSize(s, true);
            if (size < 0) continue;
            int cost = 0;
            bool ok = true;
            for (size_t i = 0; i < n && ok; i++) {
                const std::string p = s.params[i];
                if (p == "mat" || p == "same" || p == "transpose") {
                    ok = false;
                    break;
                }
                GlslType want = patternType(p, size, x.children);
                const GlslType& have = x.children[i]->type;
                if (want.base == Base::Sampler) {
                    ok = have == want;
                    continue;
                }
                // A float argument does not go to an integer family.
                if (isGeneric(p) && genericBase(p) != Base::Float && have.base == Base::Float) ok = false;
                if (ok) ok = want.known() && convertibleInHlsl(have, want);
                cost += ok ? conversionCost(have, want) : 0;
            }
            if (ok && cost < bestCost) {
                best = &s;
                bestCost = cost;
                bestSize = size;
            }
        }
        if (!best) return;
        for (size_t i = 0; i < n; i++) {
            GlslType want = patternType(best->params[i], bestSize, x.children);
            if (want.base != Base::Sampler) convertTo(x.children[i], want);
        }
        x.type = patternType(best->ret, bestSize, x.children);
    }

    const std::map<std::string, GlslType>& inputs_;
    const std::map<std::string, GlslType>& outputs_;
    std::map<std::string, GlslType> padded_;
    std::set<std::string> constants_;  // name@depth
    std::vector<std::map<std::string, GlslType>> scopes_;
    std::map<std::string, std::vector<Signature>> functions_;
    std::map<std::string, std::vector<std::pair<std::string, GlslType>>> structs_;
    GlslType returnType_;
};

}  // namespace

std::optional<ConversionResult> applyHlslConversions(const std::string& program,
                                                     const std::map<std::string, GlslType>& inputs,
                                                     const std::map<std::string, GlslType>& outputs,
                                                     std::string& error) {
    auto expanded = expandProgram(program, error);
    if (!expanded) return std::nullopt;
    auto unit = parseGlsl(expanded->tokens, error);
    if (!unit) return std::nullopt;
    Checker checker(inputs, outputs);
    checker.run(*unit);
    ConversionResult result;
    result.conversions = checker.conversions;
    for (const auto& [why, n] : checker.counts)
        result.notes.push_back("HLSL conversion: " + why + " (" + std::to_string(n) + ")");
    result.program = (expanded->version.empty() ? "#version 330 core" : expanded->version) + "\n";
    for (const auto& e : expanded->extensions) result.program += e + "\n";
    result.program += printGlsl(*unit);
    return result;
}

}  // namespace canvas::scene
