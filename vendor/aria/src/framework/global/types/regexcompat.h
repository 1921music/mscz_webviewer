// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <initializer_list>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <cstdlib>
#include <memory>
#include <utility>

extern "C" void* mscz_browser_regexp(const void* text, int textSize, const void* pattern, int patternSize,
                                    int width, int insensitive, int operation, int* size,
                                    const void* replacement, int replacementSize);
#else
#include <regex>
#endif

namespace muse::score_regex {
#ifdef __EMSCRIPTEN__
namespace regex_constants {
constexpr int ECMAScript = 0;
constexpr int icase = 1;
}
enum class Operation { Test, FullMatch, First, All, Replace };

template<typename Char>
class Pattern
{
public:
    using flag_type = int;
    static constexpr int ECMAScript = regex_constants::ECMAScript;
    static constexpr int icase = regex_constants::icase;

    Pattern() = default;
    explicit Pattern(std::basic_string<Char> text, int flags = ECMAScript)
        : m_text(std::move(text)), m_flags(flags) {}

    void* apply(const std::basic_string<Char>& text, Operation operation, int* size = nullptr,
                const std::basic_string<Char>& replacement = {}) const
    {
        return mscz_browser_regexp(text.data(), text.size(), m_text.data(), m_text.size(), sizeof(Char),
                                   m_flags & icase, static_cast<int>(operation), size,
                                   replacement.data(), replacement.size());
    }

private:
    std::basic_string<Char> m_text;
    int m_flags = ECMAScript;
};

template<typename Char>
class Match
{
public:
    Match() = default;
    Match(const std::basic_string<Char>& source, const int* spans, int count)
        : m_source(&source), m_spans(spans, spans + count * 2) {}

    bool empty() const { return m_spans.empty(); }
    size_t size() const { return m_spans.size() / 2; }
    size_t position(size_t index = 0) const
    {
        return index < size() && m_spans[index * 2] >= 0 ? m_spans[index * 2] : m_source->size();
    }
    size_t length(size_t index = 0) const
    {
        return index < size() && m_spans[index * 2] >= 0 ? m_spans[index * 2 + 1] - m_spans[index * 2] : 0;
    }
    std::basic_string<Char> str(size_t index = 0) const
    {
        return m_source ? m_source->substr(position(index), length(index)) : std::basic_string<Char>();
    }

private:
    const std::basic_string<Char>* m_source = nullptr;
    std::vector<int> m_spans;
};

using regex = Pattern<char>;
using wregex = Pattern<wchar_t>;
using smatch = Match<char>;
using wsmatch = Match<wchar_t>;

template<typename Char>
bool hasMatch(const std::basic_string<Char>& text, const Pattern<Char>& pattern)
{
    return pattern.apply(text, Operation::Test) != nullptr;
}

template<typename Char>
bool regex_match(const std::basic_string<Char>& text, const Pattern<Char>& pattern)
{
    return pattern.apply(text, Operation::FullMatch) != nullptr;
}

template<typename Char>
bool regex_search(const std::basic_string<Char>& text, Match<Char>& match, const Pattern<Char>& pattern)
{
    int size = 0;
    std::unique_ptr<int, decltype(&std::free)> data(
        static_cast<int*>(pattern.apply(text, Operation::First, &size)), std::free);
    match = size ? Match<Char>(text, data.get() + 1, *data) : Match<Char>();
    return size != 0;
}

template<typename Char>
std::vector<Match<Char>> allMatches(const std::basic_string<Char>& text, const Pattern<Char>& pattern)
{
    int size = 0;
    std::unique_ptr<int, decltype(&std::free)> data(
        static_cast<int*>(pattern.apply(text, Operation::All, &size)), std::free);
    std::vector<Match<Char>> result;
    for (int index = 0; index < size;) {
        const int count = data.get()[index++];
        result.emplace_back(text, data.get() + index, count);
        index += 2 * count;
    }
    return result;
}

template<typename Char>
std::basic_string<Char> regex_replace(const std::basic_string<Char>& text, const Pattern<Char>& pattern,
                                      const std::basic_string<Char>& replacement)
{
    int size = 0;
    std::unique_ptr<Char, decltype(&std::free)> data(
        static_cast<Char*>(pattern.apply(text, Operation::Replace, &size, replacement)), std::free);
    return size ? std::basic_string<Char>(data.get(), size) : std::basic_string<Char>();
}

template<typename Char>
std::vector<std::basic_string<Char>> tokens(const std::basic_string<Char>& text, const Pattern<Char>& pattern,
                                           std::initializer_list<int> groups)
{
    std::vector<std::basic_string<Char>> result;
    size_t previousEnd = 0;
    auto matches = allMatches(text, pattern);
    for (const auto& match : matches) {
        for (int group : groups) {
            result.push_back(group == -1 ? text.substr(previousEnd, match.position() - previousEnd) : match.str(group));
        }
        previousEnd = match.position() + match.length();
    }
    for (int group : groups) {
        if (group == -1 && (matches.empty() || previousEnd < text.size())) {
            result.push_back(text.substr(previousEnd));
            break;
        }
    }
    return result;
}
#else
using std::regex;
using std::wregex;
using std::smatch;
using std::wsmatch;
using std::regex_match;
using std::regex_replace;
using std::regex_search;
namespace regex_constants = std::regex_constants;

template<typename Char>
bool hasMatch(const std::basic_string<Char>& text, const std::basic_regex<Char>& pattern)
{
    return std::regex_search(text, pattern);
}

template<typename Char>
auto allMatches(const std::basic_string<Char>& text, const std::basic_regex<Char>& pattern)
{
    using Iterator = typename std::basic_string<Char>::const_iterator;
    using Matches = std::vector<std::match_results<Iterator>>;
    return Matches(std::regex_iterator<Iterator>(text.begin(), text.end(), pattern), {});
}

template<typename Char>
auto tokens(const std::basic_string<Char>& text, const std::basic_regex<Char>& pattern,
            std::initializer_list<int> groups)
{
    using Iterator = typename std::basic_string<Char>::const_iterator;
    using Tokens = std::vector<std::basic_string<Char>>;
    return Tokens(std::regex_token_iterator<Iterator>(text.begin(), text.end(), pattern, groups), {});
}
#endif
}
