/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace Superbird::Palette {

struct FuzzyMatch {
    int score { 0 };
    // Byte offsets into the candidate that matched, ascending and unique, for highlighting.
    std::vector<size_t> positions;
};

// Candidates longer than this are scored on their first bytes only, which keeps matching linear in practice.
inline constexpr size_t FUZZY_MAX_CANDIDATE_LENGTH = 256;

// Matches every whitespace-separated token of `query` against `candidate` as an in-order (but not necessarily
// contiguous) subsequence, ignoring ASCII case. Tokens may match in any order relative to each other. Matches at the
// start, at word boundaries, at camelCase humps, and runs of consecutive characters score higher; gaps cost a little.
// An empty query matches everything with a score of zero. Returns nothing if any token fails to match.
std::optional<FuzzyMatch> fuzzy_match(std::string_view query, std::string_view candidate);

}
