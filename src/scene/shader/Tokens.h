// Tokens and macro expansion for the HLSL conversion pass, per
// src/scene/shader/README.md section 6.1.
//
// expandProgram() takes a whole translated stage (section 6 text, with
// its #version, #extension, #define and #undef lines) and returns its code
// as tokens with every macro expanded: object like and function like
// macros, arguments expanded before substitution, a macro never expanding
// inside itself, at most 64 levels. # and ## do not exist in the dialect.
// #version and #extension lines are kept aside for the printer, #line and
// #pragma are dropped. Conditionals must already be resolved
// (ShaderPreprocessor); one left over is an error.
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace canvas::scene {

enum class TokenKind { Identifier, Int, Uint, Float, Punct };

struct Token {
    TokenKind kind = TokenKind::Punct;
    std::string text;
    int line = 0;
};

struct ExpandedProgram {
    std::string version;                 // "#version 330 core"
    std::vector<std::string> extensions;  // "#extension ..." lines
    std::vector<Token> tokens;
};

std::optional<ExpandedProgram> expandProgram(const std::string& program, std::string& error);

// Tokens of one line of code (no directives), for tests.
std::vector<Token> tokenize(const std::string& code, std::string& error);

}  // namespace canvas::scene
