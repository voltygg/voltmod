#include "Engine/BytePattern.hpp"

#include <VoltMod/Core/Log.hpp>
#include <charconv>
#include <cstring>
#include <sstream>

namespace VoltMod
{

std::vector<PatternByte> ParsePattern(const std::string& pattern)
{
    std::vector<PatternByte> bytes;
    std::istringstream stream(pattern);
    std::string token;

    while (stream >> token)
    {
        if (token == "?" || token == "??")
        {
            bytes.push_back({.Wildcard = true});
            continue;
        }

        // from_chars keeps a bad token local to this signature.
        unsigned value = 0;
        const char* end = token.data() + token.size();
        auto [stop, ec] = std::from_chars(token.data(), end, value, 16);
        if (ec != std::errc{} || stop != end || value > 0xFF)
        {
            Log::Error("Signature pattern has an invalid byte '{}'; ignoring the pattern.", token);
            return {};
        }
        bytes.push_back({.Value = static_cast<uint8_t>(value)});
    }
    return bytes;
}

void CountBytes(const uint8_t* base, size_t size, ByteHistogram& counts)
{
    for (size_t i = 0; i < size; ++i)
        ++counts[base[i]];
}

size_t AnchorOf(const std::vector<PatternByte>& pattern, const ByteHistogram& frequencies)
{
    size_t anchor = pattern.size();
    size_t rarest = SIZE_MAX;

    for (size_t i = 0; i < pattern.size(); ++i)
    {
        if (pattern[i].Wildcard)
            continue;
        if (const size_t count = frequencies[pattern[i].Value]; count < rarest)
        {
            rarest = count;
            anchor = i;
        }
    }
    return anchor;
}

/** Whether @p pattern matches at @p at, which must have room for all of it. */
static bool Matches(const uint8_t* at, const std::vector<PatternByte>& pattern)
{
    for (size_t i = 0; i < pattern.size(); ++i)
    {
        if (!pattern[i].Wildcard && at[i] != pattern[i].Value)
            return false;
    }
    return true;
}

const uint8_t* FindFirst(const uint8_t* base, size_t size, const std::vector<PatternByte>& pattern, size_t anchor)
{
    if (!base || pattern.empty() || size < pattern.size())
        return nullptr;

    const size_t lastStart = size - pattern.size();

    // Nothing to anchor on: an all-wildcard pattern matches at the first offset.
    if (anchor >= pattern.size())
        return base;

    const uint8_t wanted = pattern[anchor].Value;
    for (size_t at = 0; at <= lastStart;)
    {
        // The anchor byte sits at `at + anchor` and the last start worth testing is lastStart, so
        // the window ends at lastStart + anchor. Whatever memchr skips cannot match.
        const auto* found = static_cast<const uint8_t*>(std::memchr(base + at + anchor, wanted, lastStart - at + 1));
        if (!found)
            return nullptr;

        at = static_cast<size_t>(found - base) - anchor;
        if (Matches(base + at, pattern))
            return base + at;
        ++at;
    }
    return nullptr;
}

}  // namespace VoltMod
