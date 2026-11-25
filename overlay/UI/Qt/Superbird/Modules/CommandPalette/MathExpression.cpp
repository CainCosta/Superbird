/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Modules/CommandPalette/MathExpression.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>

namespace Superbird::Palette {

namespace {

constexpr int MAXIMUM_NESTING = 64;

// A recursive-descent parser over the grammar:
//   expression := term (('+' | '-') term)*
//   term       := unary (('*' | '/' | '%' | '×' | '÷') unary)*
//   unary      := ('+' | '-') unary | power
//   power      := primary ('^' unary)?
//   primary    := number | '(' expression ')' | constant | function '(' expression ')'
class Parser {
public:
    explicit Parser(std::string_view input)
        : m_input(input)
    {
    }

    MathResult parse()
    {
        skip_whitespace();
        if (at_end())
            return fail("Type an expression after =");
        auto value = parse_expression();
        if (!m_error.empty())
            return { {}, m_error };
        skip_whitespace();
        if (!at_end())
            return fail("Unexpected input");
        if (!std::isfinite(value))
            return fail("The result is not a finite number");
        return { value, {} };
    }

private:
    MathResult fail(char const* message)
    {
        return { {}, message };
    }

    void set_error(char const* message)
    {
        if (m_error.empty())
            m_error = message;
    }

    bool at_end() const { return m_position >= m_input.size(); }

    void skip_whitespace()
    {
        while (!at_end() && (m_input[m_position] == ' ' || m_input[m_position] == '\t'))
            ++m_position;
    }

    bool consume(std::string_view token)
    {
        skip_whitespace();
        if (m_input.substr(m_position, token.size()) == token) {
            m_position += token.size();
            return true;
        }
        return false;
    }

    double parse_expression()
    {
        if (++m_depth > MAXIMUM_NESTING) {
            set_error("The expression is nested too deeply");
            return 0;
        }
        auto value = parse_term();
        while (m_error.empty()) {
            if (consume("+"))
                value += parse_term();
            else if (consume("-"))
                value -= parse_term();
            else
                break;
        }
        --m_depth;
        return value;
    }

    double parse_term()
    {
        auto value = parse_unary();
        while (m_error.empty()) {
            if (consume("*") || consume("\xC3\x97")) {
                value *= parse_unary();
            } else if (consume("/") || consume("\xC3\xB7")) {
                auto divisor = parse_unary();
                if (divisor == 0) {
                    set_error("Division by zero");
                    return 0;
                }
                value /= divisor;
            } else if (consume("%")) {
                auto divisor = parse_unary();
                if (divisor == 0) {
                    set_error("Division by zero");
                    return 0;
                }
                value = std::fmod(value, divisor);
            } else {
                break;
            }
        }
        return value;
    }

    double parse_unary()
    {
        if (++m_depth > MAXIMUM_NESTING) {
            set_error("The expression is nested too deeply");
            return 0;
        }
        double value;
        if (consume("-"))
            value = -parse_unary();
        else if (consume("+"))
            value = parse_unary();
        else
            value = parse_power();
        --m_depth;
        return value;
    }

    double parse_power()
    {
        auto base = parse_primary();
        if (m_error.empty() && consume("^")) {
            auto exponent = parse_unary();
            return std::pow(base, exponent);
        }
        return base;
    }

    double parse_primary()
    {
        skip_whitespace();
        if (at_end()) {
            set_error("The expression ends too soon");
            return 0;
        }

        auto c = m_input[m_position];
        if (c == '(') {
            ++m_position;
            auto value = parse_expression();
            if (m_error.empty() && !consume(")"))
                set_error("Missing )");
            return value;
        }
        if ((c >= '0' && c <= '9') || c == '.')
            return parse_number();
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
            return parse_identifier();

        set_error("Unexpected input");
        return 0;
    }

    // NB: Parsed by hand rather than with strtod(), which follows the locale's decimal separator.
    double parse_number()
    {
        uint64_t mantissa = 0;
        int decimal_exponent = 0;
        int significant_digits = 0;
        bool saw_digit = false;

        auto add_digit = [&](char digit, bool is_fraction) {
            saw_digit = true;
            if (significant_digits < 19) {
                if (mantissa != 0 || digit != '0')
                    ++significant_digits;
                mantissa = mantissa * 10 + static_cast<uint64_t>(digit - '0');
                if (is_fraction)
                    --decimal_exponent;
            } else if (!is_fraction) {
                ++decimal_exponent;
            }
        };

        while (!at_end() && m_input[m_position] >= '0' && m_input[m_position] <= '9')
            add_digit(m_input[m_position++], false);
        if (!at_end() && m_input[m_position] == '.') {
            ++m_position;
            while (!at_end() && m_input[m_position] >= '0' && m_input[m_position] <= '9')
                add_digit(m_input[m_position++], true);
        }
        if (!saw_digit) {
            set_error("Malformed number");
            return 0;
        }

        // An exponent only counts when digits follow it; otherwise the `e` is left for the caller (and rejected there).
        if (!at_end() && (m_input[m_position] == 'e' || m_input[m_position] == 'E')) {
            auto lookahead = m_position + 1;
            bool negative = false;
            if (lookahead < m_input.size() && (m_input[lookahead] == '+' || m_input[lookahead] == '-')) {
                negative = m_input[lookahead] == '-';
                ++lookahead;
            }
            if (lookahead < m_input.size() && m_input[lookahead] >= '0' && m_input[lookahead] <= '9') {
                int exponent = 0;
                while (lookahead < m_input.size() && m_input[lookahead] >= '0' && m_input[lookahead] <= '9') {
                    if (exponent < 10000)
                        exponent = exponent * 10 + (m_input[lookahead] - '0');
                    ++lookahead;
                }
                decimal_exponent += negative ? -exponent : exponent;
                m_position = lookahead;
            }
        }

        auto value = static_cast<double>(mantissa);
        if (decimal_exponent > 0)
            value *= std::pow(10.0, decimal_exponent);
        else if (decimal_exponent < 0)
            value /= std::pow(10.0, -decimal_exponent);
        return value;
    }

    double parse_identifier()
    {
        auto start = m_position;
        while (!at_end()) {
            auto c = m_input[m_position];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
                break;
            ++m_position;
        }
        auto name = m_input.substr(start, m_position - start);

        if (name == "pi" || name == "PI")
            return std::numbers::pi;
        if (name == "e" || name == "E")
            return std::numbers::e;

        using UnaryFunction = double (*)(double);
        struct Function {
            std::string_view name;
            UnaryFunction function;
        };
        static constexpr Function functions[] = {
            { "sqrt", [](double x) { return std::sqrt(x); } },
            { "abs", [](double x) { return std::fabs(x); } },
            { "round", [](double x) { return std::round(x); } },
            { "floor", [](double x) { return std::floor(x); } },
            { "ceil", [](double x) { return std::ceil(x); } },
            { "ln", [](double x) { return std::log(x); } },
            { "log", [](double x) { return std::log10(x); } },
            { "sin", [](double x) { return std::sin(x); } },
            { "cos", [](double x) { return std::cos(x); } },
            { "tan", [](double x) { return std::tan(x); } },
        };

        for (auto const& function : functions) {
            if (function.name != name)
                continue;
            if (!consume("(")) {
                set_error("Expected ( after a function name");
                return 0;
            }
            auto argument = parse_expression();
            if (m_error.empty() && !consume(")")) {
                set_error("Missing )");
                return 0;
            }
            auto value = function.function(argument);
            if (!std::isfinite(value)) {
                set_error("The result is not a finite number");
                return 0;
            }
            return value;
        }

        set_error("Unknown name");
        return 0;
    }

    std::string_view m_input;
    size_t m_position { 0 };
    int m_depth { 0 };
    std::string m_error;
};

}

MathResult evaluate_math_expression(std::string_view input)
{
    return Parser(input).parse();
}

std::string format_math_result(double value)
{
    if (value == 0)
        return "0";

    char buffer[64];
    if (std::fabs(value) < 1e15 && value == std::floor(value)) {
        std::snprintf(buffer, sizeof(buffer), "%.0f", value);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.12g", value);
    }

    // NB: snprintf() uses the locale's decimal separator, which Qt sets from the environment. Results always use '.'.
    std::string result(buffer);
    for (auto& c : result) {
        if (c == ',')
            c = '.';
    }
    return result;
}

}
