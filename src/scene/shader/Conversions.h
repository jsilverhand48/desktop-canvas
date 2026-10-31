// The HLSL conversion pass of src/scene/shader/README.md section 6.1.
//
// applyHlslConversions() takes one translated stage (section 6 output:
// #version, #extension, the combo and prelude #defines, then the code),
// expands its macros (Tokens.h), parses it (Syntax.h) and type checks it,
// inserting a GLSL constructor wherever HLSL would convert implicitly and
// GLSL does not: splat, truncation, numeric and bool conversions, the
// fmod meaning of % on floats, int array indices, swizzles of scalars.
// `inputs` widens fragment inputs to the vertex stage's types and
// `outputs` widens vertex outputs to the fragment stage's (names to
// types; assignments to a widened output are padded with zeros). Local
// const variables with non constant initializers lose const (HLSL allows
// them, GLSL does not). The result is printed GLSL (#version and
// #extension lines, then the code, comments and #defines gone) with notes
// counting what changed.
// It returns nullopt with an error when the stage does not expand or
// parse; the caller then keeps its input.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "scene/shader/Syntax.h"

namespace canvas::scene {

struct ConversionResult {
    std::string program;
    std::vector<std::string> notes;
    size_t conversions = 0;
};

std::optional<ConversionResult> applyHlslConversions(const std::string& program,
                                                     const std::map<std::string, GlslType>& inputs,
                                                     const std::map<std::string, GlslType>& outputs,
                                                     std::string& error);

}  // namespace canvas::scene
