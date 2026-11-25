/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace Superbird::Palette {

struct MathResult {
    std::optional<double> value;
    // Why the expression has no value, for showing under the input. Empty when `value` is set.
    std::string error;

    bool ok() const { return value.has_value(); }
};

// Evaluates the arithmetic of a Command Palette `=` query:
//   numbers (`12`, `1.5`, `.5`, `2e3`), `+ - * / % ^` (`^` is right-associative and binds tighter than unary minus,
//   so `-2^2` is -4), `×` and `÷`, parentheses, unary `+`/`-`, the constants `pi` and `e`, and the functions
//   `sqrt abs round floor ceil ln log sin cos tan` (radians). `%` is the remainder.
// Never throws, and never returns NaN or infinity: those become errors.
MathResult evaluate_math_expression(std::string_view);

// Formats a result for display: integers without a fractional part, other values with up to 12 significant digits
// and no trailing zeros, and very large or small magnitudes in exponent form.
std::string format_math_result(double);

}
