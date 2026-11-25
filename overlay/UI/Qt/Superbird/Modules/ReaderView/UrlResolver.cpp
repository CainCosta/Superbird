/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Modules/ReaderView/UrlResolver.h>

#include <vector>

namespace Superbird::Reader {

namespace {

constexpr bool is_ascii_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
constexpr bool is_ascii_digit(char c) { return c >= '0' && c <= '9'; }
constexpr char ascii_lowercase(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c; }

// Browsers strip leading/trailing C0 controls and spaces, and remove tabs and newlines anywhere, before parsing.
std::string clean(std::string_view input)
{
    size_t start = 0;
    size_t end = input.size();
    while (start < end && static_cast<unsigned char>(input[start]) <= 0x20)
        ++start;
    while (end > start && static_cast<unsigned char>(input[end - 1]) <= 0x20)
        --end;
    std::string result;
    result.reserve(end - start);
    for (size_t i = start; i < end; ++i) {
        auto c = input[i];
        if (c == '\t' || c == '\n' || c == '\r')
            continue;
        result.push_back(c);
    }
    return result;
}

struct Components {
    std::optional<std::string> scheme;
    std::optional<std::string> authority;
    std::string path;
    std::optional<std::string> query;
    std::optional<std::string> fragment;
};

// The scheme of a cleaned URL: letters, then letters/digits/+-., then ':'.
std::optional<size_t> scheme_length(std::string_view url)
{
    if (url.empty() || !is_ascii_alpha(url[0]))
        return {};
    for (size_t i = 1; i < url.size(); ++i) {
        auto c = url[i];
        if (c == ':')
            return i;
        if (!is_ascii_alpha(c) && !is_ascii_digit(c) && c != '+' && c != '-' && c != '.')
            return {};
    }
    return {};
}

Components split(std::string_view url)
{
    Components components;
    if (auto length = scheme_length(url); length.has_value()) {
        std::string scheme;
        for (size_t i = 0; i < *length; ++i)
            scheme.push_back(ascii_lowercase(url[i]));
        components.scheme = std::move(scheme);
        url.remove_prefix(*length + 1);
    }
    if (auto hash = url.find('#'); hash != std::string_view::npos) {
        components.fragment = std::string(url.substr(hash + 1));
        url = url.substr(0, hash);
    }
    if (auto question = url.find('?'); question != std::string_view::npos) {
        components.query = std::string(url.substr(question + 1));
        url = url.substr(0, question);
    }
    if (url.substr(0, 2) == "//") {
        url.remove_prefix(2);
        auto slash = url.find('/');
        components.authority = std::string(url.substr(0, slash));
        url = slash == std::string_view::npos ? std::string_view {} : url.substr(slash);
    }
    components.path = std::string(url);
    return components;
}

// RFC 3986 5.2.4.
std::string remove_dot_segments(std::string_view path)
{
    std::vector<std::string_view> output;
    bool absolute = !path.empty() && path.front() == '/';
    bool trailing_slash = false;
    size_t start = absolute ? 1 : 0;
    while (start <= path.size()) {
        auto end = path.find('/', start);
        if (end == std::string_view::npos)
            end = path.size();
        auto segment = path.substr(start, end - start);
        bool is_last = end == path.size();
        if (segment == ".") {
            trailing_slash = is_last;
        } else if (segment == "..") {
            if (!output.empty())
                output.pop_back();
            trailing_slash = is_last;
        } else {
            output.push_back(segment);
            trailing_slash = false;
        }
        start = end + 1;
    }
    std::string result = absolute ? "/" : "";
    for (size_t i = 0; i < output.size(); ++i) {
        if (i > 0)
            result.push_back('/');
        result += output[i];
    }
    if (trailing_slash && (result.empty() || result.back() != '/'))
        result.push_back('/');
    return result;
}

std::string merge_paths(Components const& base, std::string_view reference_path)
{
    if (base.authority.has_value() && base.path.empty())
        return "/" + std::string(reference_path);
    auto last_slash = base.path.rfind('/');
    if (last_slash == std::string::npos)
        return std::string(reference_path);
    return base.path.substr(0, last_slash + 1) + std::string(reference_path);
}

std::string recompose(Components const& components)
{
    std::string result;
    if (components.scheme.has_value())
        result += *components.scheme + ":";
    if (components.authority.has_value())
        result += "//" + *components.authority;
    result += components.path;
    if (components.query.has_value())
        result += "?" + *components.query;
    if (components.fragment.has_value())
        result += "#" + *components.fragment;
    return result;
}

bool is_web_scheme(std::string const& scheme)
{
    return scheme == "http" || scheme == "https";
}

}

std::optional<std::string> url_scheme(std::string_view url)
{
    auto cleaned = clean(url);
    auto length = scheme_length(cleaned);
    if (!length.has_value())
        return {};
    std::string scheme;
    for (size_t i = 0; i < *length; ++i)
        scheme.push_back(ascii_lowercase(cleaned[i]));
    return scheme;
}

std::optional<std::string> resolve_url(std::string_view base_input, std::string_view reference_input)
{
    auto reference_string = clean(reference_input);
    auto reference_scheme = url_scheme(reference_string);
    // Web URLs treat backslashes as slashes; others (mailto:, data:, ...) keep them.
    auto base_string = clean(base_input);
    auto base_scheme = url_scheme(base_string);
    bool web = reference_scheme.has_value() ? is_web_scheme(*reference_scheme) : (base_scheme.has_value() && is_web_scheme(*base_scheme));
    if (web) {
        for (auto& c : reference_string) {
            if (c == '\\')
                c = '/';
        }
    }

    auto reference = split(reference_string);
    if (reference.scheme.has_value()) {
        // "http:foo" with the base's own web scheme is relative in browsers; any other scheme makes it absolute.
        if (!(base_scheme.has_value() && *reference.scheme == *base_scheme && is_web_scheme(*base_scheme) && !reference.authority.has_value())) {
            reference.path = remove_dot_segments(reference.path);
            if (is_web_scheme(*reference.scheme) && reference.path.empty() && reference.authority.has_value())
                reference.path = "/";
            return recompose(reference);
        }
        reference.scheme.reset();
    }

    if (!base_scheme.has_value())
        return {};
    auto base = split(base_string);

    Components target;
    target.scheme = base.scheme;
    target.fragment = reference.fragment;
    if (reference.authority.has_value()) {
        target.authority = reference.authority;
        target.path = remove_dot_segments(reference.path);
        target.query = reference.query;
    } else {
        target.authority = base.authority;
        if (reference.path.empty()) {
            target.path = base.path;
            target.query = reference.query.has_value() ? reference.query : base.query;
        } else {
            if (reference.path.front() == '/')
                target.path = remove_dot_segments(reference.path);
            else
                target.path = remove_dot_segments(merge_paths(base, reference.path));
            target.query = reference.query;
        }
    }
    if (target.scheme.has_value() && is_web_scheme(*target.scheme) && target.path.empty())
        target.path = "/";
    return recompose(target);
}

std::optional<std::string> safe_link_url(std::string_view base, std::string_view reference)
{
    auto cleaned = clean(reference);
    if (!cleaned.empty() && cleaned.front() == '#')
        return cleaned;
    auto resolved = resolve_url(base, cleaned);
    if (!resolved.has_value())
        return {};
    auto scheme = url_scheme(*resolved);
    if (!scheme.has_value() || !(is_web_scheme(*scheme) || *scheme == "mailto"))
        return {};
    return resolved;
}

std::optional<std::string> safe_image_url(std::string_view base, std::string_view reference)
{
    auto resolved = resolve_url(base, reference);
    if (!resolved.has_value())
        return {};
    auto scheme = url_scheme(*resolved);
    if (!scheme.has_value() || !is_web_scheme(*scheme))
        return {};
    return resolved;
}

}
