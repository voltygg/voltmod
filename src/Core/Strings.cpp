#include <VoltMod/Core/Strings.hpp>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>
#include <sstream>
#include <string_view>

namespace VoltMod
{

std::optional<int64_t> ParseInt64(std::string_view text)
{
    int64_t value{};
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size())
        return std::nullopt;
    return value;
}

std::optional<uint64_t> ParseUInt64(std::string_view text)
{
    uint64_t value{};
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size())
        return std::nullopt;
    return value;
}

int ParseDuration(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);

    if (text.empty())
        return -1;

    std::string lower(text);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    if (lower == "0" || lower == "perm" || lower == "permanent")
        return 0;

    int64_t multiplier = 1;
    char suffix = lower.back();
    if (!std::isdigit(static_cast<unsigned char>(suffix)))
    {
        switch (suffix)
        {
        case 's':
            multiplier = 1;
            break;
        case 'm':
            multiplier = 60;
            break;
        case 'h':
            multiplier = 3600;
            break;
        case 'd':
            multiplier = 86400;
            break;
        case 'w':
            multiplier = 604800;
            break;
        default:
            return -1;
        }
        lower.pop_back();
    }

    int value = 0;
    auto [ptr, ec] = std::from_chars(lower.data(), lower.data() + lower.size(), value);
    if (ec != std::errc{} || ptr != lower.data() + lower.size() || value < 0)
        return -1;

    int64_t total = static_cast<int64_t>(value) * multiplier;
    if (total > std::numeric_limits<int>::max())
        return -1;
    return static_cast<int>(total);
}

std::string Strings::ToLower(std::string_view str)
{
    std::string result(str);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
    return result;
}

std::string Strings::Trim(std::string_view str)
{
    auto begin = std::find_if(str.begin(), str.end(), [](unsigned char c) { return !std::isspace(c); });
    auto end = std::find_if(str.rbegin(), str.rend(), [](unsigned char c) { return !std::isspace(c); }).base();
    return begin < end ? std::string(begin, end) : std::string();
}

std::string Strings::Join(const std::vector<std::string>& parts, std::string_view delimiter)
{
    if (parts.empty())
        return "";

    std::string result = parts[0];
    for (size_t i = 1; i < parts.size(); ++i)
    {
        result += delimiter;
        result += parts[i];
    }
    return result;
}

std::string Strings::JoinNonEmpty(const std::vector<std::string>& parts, std::string_view delimiter)
{
    std::string result;
    for (const auto& part : parts)
    {
        if (part.empty())
            continue;
        if (!result.empty())
            result += delimiter;
        result += part;
    }
    return result;
}

bool Strings::StartsWith(std::string_view str, std::string_view prefix)
{
    return str.starts_with(prefix);
}

bool Strings::ContainsIgnoreCase(std::string_view str, std::string_view substr)
{
    return ToLower(str).find(ToLower(substr)) != std::string::npos;
}

// SubstituteTokens is the only caller; not public API.
static std::string ReplaceAll(const std::string& str, const std::string& from, const std::string& to)
{
    if (from.empty())
        return str;

    std::string result = str;
    size_t pos = 0;
    while ((pos = result.find(from, pos)) != std::string::npos)
    {
        result.replace(pos, from.length(), to);
        pos += to.length();
    }
    return result;
}

std::string Strings::SubstituteTokens(std::string text, const std::map<std::string, std::string>& tokens)
{
    for (const auto& [key, value] : tokens)
        text = ReplaceAll(text, "{" + key + "}", value);
    return text;
}

std::string Strings::EscapeHtml(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text)
    {
        switch (c)
        {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '\'':
            out += "&#39;";
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

std::string Strings::SanitizeUtf8(std::string_view text, std::string_view replacement)
{
    std::string out;
    out.reserve(text.size());

    const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
    const std::size_t size = text.size();

    for (std::size_t i = 0; i < size;)
    {
        const unsigned char lead = bytes[i];

        // How many continuation bytes this lead announces, and the smallest code point it may
        // legally encode. Rejecting an overlong encoding matters: a two-byte encoding of NUL
        // would otherwise slip past a later check that only inspects the decoded string.
        std::size_t extra = 0;
        char32_t code = 0;
        char32_t lowest = 0;
        if (lead < 0x80)
        {
            out.push_back(static_cast<char>(lead));
            ++i;
            continue;
        }
        else if ((lead & 0xE0) == 0xC0)
        {
            extra = 1;
            code = lead & 0x1Fu;
            lowest = 0x80;
        }
        else if ((lead & 0xF0) == 0xE0)
        {
            extra = 2;
            code = lead & 0x0Fu;
            lowest = 0x800;
        }
        else if ((lead & 0xF8) == 0xF0)
        {
            extra = 3;
            code = lead & 0x07u;
            lowest = 0x10000;
        }
        else
        {
            // A continuation byte with no lead, or one of the 5/6-byte forms UTF-8 never had.
            out.append(replacement);
            ++i;
            continue;
        }

        if (i + extra >= size)
        {
            // Truncated sequence: the string ends mid-character.
            out.append(replacement);
            ++i;
            continue;
        }

        bool wellFormed = true;
        for (std::size_t k = 1; k <= extra; ++k)
        {
            const unsigned char continuation = bytes[i + k];
            if ((continuation & 0xC0) != 0x80)
            {
                wellFormed = false;
                break;
            }
            code = (code << 6) | (continuation & 0x3Fu);
        }

        // Surrogates are unencodable in UTF-8, and nothing above U+10FFFF exists.
        if (!wellFormed || code < lowest || (code >= 0xD800 && code <= 0xDFFF) || code > 0x10FFFF)
        {
            out.append(replacement);
            ++i;
            continue;
        }

        out.append(text.substr(i, extra + 1));
        i += extra + 1;
    }

    return out;
}

std::string Strings::TruncateUtf8(std::string_view text, std::size_t maxBytes, std::string_view ellipsis)
{
    if (text.size() <= maxBytes)
        return SanitizeUtf8(text);
    std::size_t end = maxBytes;
    // Back up past UTF-8 continuation bytes so the cut never splits a multibyte sequence.
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
        --end;
    return SanitizeUtf8(text.substr(0, end)).append(ellipsis);
}

bool Strings::IsNumeric(std::string_view str)
{
    if (str.empty())
        return false;
    return std::all_of(str.begin(), str.end(), [](unsigned char c) { return std::isdigit(c); });
}

std::string Strings::DisplayNameOr(int64_t id, std::string_view name, std::size_t maxBytes)
{
    // A name is player-controlled and this is row markup, so it is escaped here rather than at
    // each call site: an unescaped '<' would let a player close the row's font tag.
    return name.empty() ? std::to_string(id) : EscapeHtml(TruncateUtf8(name, maxBytes));
}

}  // namespace VoltMod
