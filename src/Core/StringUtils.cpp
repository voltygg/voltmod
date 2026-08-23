#include <CS2Kit/Core/SteamId.hpp>
#include <CS2Kit/Core/StringUtils.hpp>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>
#include <sstream>

namespace CS2Kit::Core
{

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

std::string StringUtils::ToLower(const std::string& str)
{
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
    return result;
}

std::string StringUtils::ToUpper(const std::string& str)
{
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::toupper(c); });
    return result;
}

std::string StringUtils::TrimLeft(const std::string& str)
{
    auto it = std::find_if(str.begin(), str.end(), [](unsigned char c) { return !std::isspace(c); });
    return std::string(it, str.end());
}

std::string StringUtils::TrimRight(const std::string& str)
{
    auto it = std::find_if(str.rbegin(), str.rend(), [](unsigned char c) { return !std::isspace(c); });
    return std::string(str.begin(), it.base());
}

std::string StringUtils::Trim(const std::string& str)
{
    return TrimLeft(TrimRight(str));
}

std::vector<std::string> StringUtils::Split(const std::string& str, char delimiter)
{
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, delimiter))
        result.push_back(item);
    return result;
}

std::string StringUtils::Join(const std::vector<std::string>& parts, const std::string& delimiter)
{
    if (parts.empty())
        return "";

    std::string result = parts[0];
    for (size_t i = 1; i < parts.size(); ++i)
        result += delimiter + parts[i];
    return result;
}

bool StringUtils::StartsWith(const std::string& str, const std::string& prefix)
{
    return str.length() >= prefix.length() && str.substr(0, prefix.length()) == prefix;
}

bool StringUtils::EndsWith(const std::string& str, const std::string& suffix)
{
    return str.length() >= suffix.length() && str.substr(str.length() - suffix.length()) == suffix;
}

bool StringUtils::ContainsIgnoreCase(const std::string& str, const std::string& substr)
{
    return ToLower(str).find(ToLower(substr)) != std::string::npos;
}

std::string StringUtils::ReplaceAll(const std::string& str, const std::string& from, const std::string& to)
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

std::string StringUtils::SubstituteTokens(std::string text, const std::map<std::string, std::string>& tokens)
{
    for (const auto& [key, value] : tokens)
        text = ReplaceAll(text, "{" + key + "}", value);
    return text;
}

std::string StringUtils::EscapeHtml(const std::string& text)
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

std::string StringUtils::TruncateUtf8(const std::string& text, std::size_t maxBytes, std::string_view ellipsis)
{
    if (text.size() <= maxBytes)
        return text;
    std::size_t end = maxBytes;
    // Back up past UTF-8 continuation bytes so the cut never splits a multibyte sequence.
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
        --end;
    return text.substr(0, end).append(ellipsis);
}

bool StringUtils::IsNumeric(const std::string& str)
{
    if (str.empty())
        return false;
    return std::all_of(str.begin(), str.end(), [](unsigned char c) { return std::isdigit(c); });
}

std::string StringUtils::DisplayNameOr(int64_t id, const std::string& name, std::size_t maxBytes)
{
    return name.empty() ? std::to_string(id) : TruncateUtf8(name, maxBytes);
}

}  // namespace CS2Kit::Core
