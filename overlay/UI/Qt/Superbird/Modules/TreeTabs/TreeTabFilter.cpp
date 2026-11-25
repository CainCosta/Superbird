/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Modules/TreeTabs/TreeTabFilter.h>

namespace Superbird::TreeTabs {

static constexpr char ascii_lowercase(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

static bool contains_ignoring_case(std::string_view haystack, std::string_view needle)
{
    if (needle.size() > haystack.size())
        return false;
    for (size_t start = 0; start + needle.size() <= haystack.size(); ++start) {
        size_t i = 0;
        while (i < needle.size() && ascii_lowercase(haystack[start + i]) == ascii_lowercase(needle[i]))
            ++i;
        if (i == needle.size())
            return true;
    }
    return false;
}

bool tab_matches_filter(std::string_view title, std::string_view url, std::string_view query)
{
    size_t start = 0;
    while (start < query.size()) {
        while (start < query.size() && (query[start] == ' ' || query[start] == '\t'))
            ++start;
        auto end = start;
        while (end < query.size() && query[end] != ' ' && query[end] != '\t')
            ++end;
        if (end == start)
            break;
        auto word = query.substr(start, end - start);
        if (!contains_ignoring_case(title, word) && !contains_ignoring_case(url, word))
            return false;
        start = end;
    }
    return true;
}

}
