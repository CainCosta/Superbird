/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <string_view>

namespace Superbird::TreeTabs {

// The sidebar quick filter's rule: every whitespace-separated word of the query appears (ignoring ASCII case) in the
// tab's title or its URL. An empty or all-whitespace query matches every tab.
bool tab_matches_filter(std::string_view title, std::string_view url, std::string_view query);

}
