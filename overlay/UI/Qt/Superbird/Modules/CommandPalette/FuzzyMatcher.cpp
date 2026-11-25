/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Modules/CommandPalette/FuzzyMatcher.h>

#include <algorithm>
#include <climits>

namespace Superbird::Palette {

namespace {

constexpr int SCORE_MATCH = 16;
constexpr int BONUS_FIRST_CHARACTER = 16;
constexpr int BONUS_WORD_BOUNDARY = 12;
constexpr int BONUS_CAMEL_CASE = 8;
constexpr int BONUS_CONSECUTIVE = 10;
constexpr int PENALTY_GAP_START = 3;
constexpr int PENALTY_GAP_EXTEND = 1;
// Gaps longer than this cost no more, so a match far along a long URL is still worth something.
constexpr int MAXIMUM_GAP_EXTENSIONS = 12;
constexpr int UNMATCHED = INT_MIN / 4;

constexpr char ascii_lowercase(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

constexpr bool is_ascii_upper(char c) { return c >= 'A' && c <= 'Z'; }
constexpr bool is_ascii_lower(char c) { return c >= 'a' && c <= 'z'; }
constexpr bool is_ascii_digit(char c) { return c >= '0' && c <= '9'; }
constexpr bool is_ascii_alphanumeric(char c) { return is_ascii_upper(c) || is_ascii_lower(c) || is_ascii_digit(c); }

constexpr bool is_separator(char c)
{
    switch (c) {
    case ' ':
    case '\t':
    case '-':
    case '_':
    case '/':
    case '\\':
    case '.':
    case ':':
    case ',':
    case '(':
    case ')':
    case '[':
    case ']':
    case '|':
    case '?':
    case '#':
    case '&':
    case '=':
    case '@':
    case '+':
        return true;
    default:
        return false;
    }
}

int position_bonus(std::string_view candidate, size_t index)
{
    if (index == 0)
        return BONUS_FIRST_CHARACTER;
    auto previous = candidate[index - 1];
    auto current = candidate[index];
    if (is_separator(previous))
        return BONUS_WORD_BOUNDARY;
    if (is_ascii_lower(previous) && is_ascii_upper(current))
        return BONUS_CAMEL_CASE;
    if (!is_ascii_alphanumeric(previous) && is_ascii_alphanumeric(current))
        return BONUS_WORD_BOUNDARY / 2;
    return 0;
}

std::optional<FuzzyMatch> match_token(std::string_view token, std::string_view candidate)
{
    auto const m = token.size();
    auto const n = std::min(candidate.size(), FUZZY_MAX_CANDIDATE_LENGTH);
    if (m == 0)
        return FuzzyMatch {};
    if (m > n)
        return {};

    // Cheap rejection before the dynamic program: the token must be a subsequence at all.
    {
        size_t t = 0;
        for (size_t j = 0; j < n && t < m; ++j) {
            if (ascii_lowercase(candidate[j]) == ascii_lowercase(token[t]))
                ++t;
        }
        if (t < m)
            return {};
    }

    // best[j]: the best score of matching token[0..i] with token[i] landing on candidate[j].
    std::vector<int> previous(n, UNMATCHED);
    std::vector<int> current(n, UNMATCHED);
    // The bonus of the character that started the run of consecutive matches ending at j. Every character in a run
    // earns at least that much, so a whole word matched from its start beats initials scattered across the candidate.
    std::vector<int> previous_run_bonus(n, 0);
    std::vector<int> current_run_bonus(n, 0);
    // came_from[i * n + j]: where token[i - 1] landed on the best path through (i, j).
    std::vector<int> came_from(m * n, -1);

    for (size_t j = 0; j < n; ++j) {
        if (ascii_lowercase(candidate[j]) != ascii_lowercase(token[0]))
            continue;
        // A token that starts further in is slightly worse, all else being equal.
        auto leading_penalty = static_cast<int>(std::min<size_t>(j, 9) / 3);
        previous_run_bonus[j] = position_bonus(candidate, j);
        previous[j] = SCORE_MATCH + previous_run_bonus[j] - leading_penalty;
    }

    for (size_t i = 1; i < m; ++i) {
        std::fill(current.begin(), current.end(), UNMATCHED);
        std::fill(current_run_bonus.begin(), current_run_bonus.end(), 0);
        auto lowered = ascii_lowercase(token[i]);

        // The best score reaching j through a gap of at least one character, and where that path left from.
        int gap_score = UNMATCHED;
        int gap_from = -1;

        for (size_t j = i; j < n; ++j) {
            if (gap_score != UNMATCHED && static_cast<int>(j) - gap_from - 1 <= MAXIMUM_GAP_EXTENSIONS)
                gap_score -= PENALTY_GAP_EXTEND;
            if (j >= 2 && previous[j - 2] != UNMATCHED) {
                auto opened = previous[j - 2] - PENALTY_GAP_START;
                if (opened > gap_score) {
                    gap_score = opened;
                    gap_from = static_cast<int>(j - 2);
                }
            }

            if (ascii_lowercase(candidate[j]) != lowered)
                continue;

            auto own_bonus = position_bonus(candidate, j);
            int best = UNMATCHED;
            int best_from = -1;
            int best_run_bonus = own_bonus;
            if (previous[j - 1] != UNMATCHED) {
                auto run_bonus = std::max(previous_run_bonus[j - 1], own_bonus);
                best = previous[j - 1] + SCORE_MATCH + std::max(run_bonus, BONUS_CONSECUTIVE);
                best_from = static_cast<int>(j - 1);
                best_run_bonus = run_bonus;
            }
            if (gap_score != UNMATCHED && gap_score + SCORE_MATCH + own_bonus > best) {
                best = gap_score + SCORE_MATCH + own_bonus;
                best_from = gap_from;
                best_run_bonus = own_bonus;
            }
            if (best == UNMATCHED)
                continue;

            current[j] = best;
            current_run_bonus[j] = best_run_bonus;
            came_from[i * n + j] = best_from;
        }
        std::swap(previous, current);
        std::swap(previous_run_bonus, current_run_bonus);
    }

    int best_score = UNMATCHED;
    int best_end = -1;
    for (size_t j = 0; j < n; ++j) {
        if (previous[j] > best_score) {
            best_score = previous[j];
            best_end = static_cast<int>(j);
        }
    }
    if (best_end < 0)
        return {};

    FuzzyMatch match;
    match.score = best_score;
    match.positions.resize(m);
    int position = best_end;
    for (size_t i = m; i-- > 0;) {
        match.positions[i] = static_cast<size_t>(position);
        if (i > 0)
            position = came_from[i * n + static_cast<size_t>(position)];
    }
    return match;
}

}

std::optional<FuzzyMatch> fuzzy_match(std::string_view query, std::string_view candidate)
{
    FuzzyMatch combined;
    size_t start = 0;
    while (start < query.size()) {
        while (start < query.size() && (query[start] == ' ' || query[start] == '\t'))
            ++start;
        auto end = start;
        while (end < query.size() && query[end] != ' ' && query[end] != '\t')
            ++end;
        if (end == start)
            break;

        auto token_match = match_token(query.substr(start, end - start), candidate);
        if (!token_match.has_value())
            return {};
        combined.score += token_match->score;
        combined.positions.insert(combined.positions.end(), token_match->positions.begin(), token_match->positions.end());
        start = end;
    }

    std::sort(combined.positions.begin(), combined.positions.end());
    combined.positions.erase(std::unique(combined.positions.begin(), combined.positions.end()), combined.positions.end());
    return combined;
}

}
