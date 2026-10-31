// GLSL syntax for the HLSL conversion pass (src/scene/shader/README.md
// section 6.1): types, a syntax tree, a recursive descent parser over
// expanded tokens (Tokens.h) and a printer.
//
// The parser covers what the translated shaders use: global and local
// declarations with qualifiers (including layout(...)), structs, arrays,
// function prototypes and definitions, precision statements, every
// statement but switch, and the full GLSL expression grammar. Parentheses
// stay in the tree as Paren nodes, so printing reproduces the grouping
// without precedence logic and inserted conversions (calls) are always
// safe. Anything else is a parse error; callers then keep the text they
// had. The printer writes one declaration or statement per line.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "scene/shader/Tokens.h"

namespace canvas::scene {

struct GlslType {
    enum class Base { Unknown, Void, Bool, Int, Uint, Float, Sampler, Struct };
    Base base = Base::Unknown;
    int size = 1;     // vector size, 1 for scalars; matrix rows
    int columns = 0;  // matrix columns, 0 otherwise
    std::string name;  // sampler or struct type name
    int array = 0;     // array length, -1 unsized, 0 not an array

    bool known() const { return base != Base::Unknown; }
    bool numeric() const {
        return base == Base::Bool || base == Base::Int || base == Base::Uint || base == Base::Float;
    }
    bool isMatrix() const { return columns > 0 && array == 0; }
    bool isScalar() const { return numeric() && columns == 0 && size == 1 && array == 0; }
    bool isVector() const { return numeric() && columns == 0 && size > 1 && array == 0; }
    bool isScalarOrVector() const { return numeric() && columns == 0 && array == 0; }
    // GLSL spelling: float, vec3, ivec2, bvec4, mat3, mat4x3, a sampler or
    // struct name (array suffix not included).
    std::string glsl() const;
    GlslType element() const;  // without the array
    bool operator==(const GlslType& o) const;
    bool operator!=(const GlslType& o) const { return !(*this == o); }

    static GlslType scalar(Base b) { GlslType t; t.base = b; return t; }
    static GlslType vector(Base b, int n) { GlslType t; t.base = b; t.size = n; return t; }
};

// A built in type name (float, vec3, mat4x3, sampler2D, ...); Unknown for
// other names.
GlslType builtinType(const std::string& name);

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct Expr {
    enum class Kind {
        Literal, Identifier, Paren, Unary, Postfix, Binary, Assign, Ternary, Call, Index,
        Member, Comma, Fmod
    };
    Kind kind = Kind::Literal;
    std::string text;  // literal, identifier, operator, member or callee name
    TokenKind literal = TokenKind::Int;
    std::vector<ExprPtr> children;
    bool arrayConstructor = false;  // callee written type[N](...)
    ExprPtr arraySize;              // its N (may be null: type[](...))
    GlslType type;                  // set by the checker
    int line = 0;
};

struct StructDef;
struct Declaration;

struct TypeSpec {
    std::string name;                    // vec3, float, a struct name
    std::shared_ptr<StructDef> structDef;  // struct defined in place
    bool hasArray = false;               // float[3] name
    ExprPtr arraySize;
};

struct Declarator {
    std::string name;
    bool hasArray = false;
    ExprPtr arraySize;  // null for name[]
    ExprPtr init;
};

struct Declaration {
    std::vector<std::string> qualifiers;  // as written, e.g. "layout(location = 0)", "uniform"
    TypeSpec type;
    std::vector<Declarator> vars;  // empty for a struct definition alone
    int line = 0;
};

struct StructDef {
    std::string name;
    std::vector<Declaration> members;
};

struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;

struct Stmt {
    enum class Kind {
        Block, Declaration, Expression, If, For, While, DoWhile, Return, Break, Continue,
        Discard, Empty
    };
    Kind kind = Kind::Empty;
    std::vector<StmtPtr> body;  // Block: statements; If: then (, else); loops: body
    std::unique_ptr<Declaration> decl;
    ExprPtr expr;  // expression, condition or return value
    StmtPtr init;  // for: init statement
    ExprPtr step;  // for: iteration expression
    int line = 0;
};

struct Parameter {
    std::vector<std::string> qualifiers;
    TypeSpec type;
    std::string name;  // may be empty in prototypes
    bool hasArray = false;
    ExprPtr arraySize;
};

struct Function {
    std::vector<std::string> qualifiers;
    TypeSpec returnType;
    std::string name;
    std::vector<Parameter> params;
    StmtPtr body;  // null for a prototype
    int line = 0;
};

struct TopLevel {
    enum class Kind { Declaration, Function, Precision };
    Kind kind = Kind::Declaration;
    std::unique_ptr<Declaration> decl;
    std::unique_ptr<Function> function;
    std::string text;  // precision statement
};

struct TranslationUnit {
    std::vector<TopLevel> items;
};

std::optional<TranslationUnit> parseGlsl(const std::vector<Token>& tokens, std::string& error);
std::string printGlsl(const TranslationUnit& unit);
std::string printExpr(const Expr& e);

}  // namespace canvas::scene
