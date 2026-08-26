#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/**
 * Parse a human duration string into seconds. Accepts a bare integer (seconds) or an
 * integer with a unit suffix: `s` seconds, `m` minutes, `h` hours, `d` days, `w` weeks
 * (case-insensitive). The literals `0`, `perm`, and `permanent` mean "permanent" (returns 0).
 * Surrounding whitespace is ignored. This is the single canonical duration grammar;
 * Time::ParseDuration delegates here.
 * Returns -1 on parse failure or int overflow, 0 for permanent, otherwise seconds.
 */
int ParseDuration(std::string_view text);

/** @brief Collection of static string manipulation utilities. */
class Strings
{
public:
    static std::string ToLower(const std::string& str);
    static std::string Trim(const std::string& str);
    static std::vector<std::string> Split(const std::string& str, char delimiter);
    static std::string Join(const std::vector<std::string>& parts, const std::string& delimiter);

    /**
     * Join @p parts with @p delimiter, skipping empty ones.
     *
     * For text assembled from optional pieces - a ban notice's reason, expiry and appeal link,
     * say. Skipping is the point: a piece that is not configured must not leave a dangling
     * delimiter or a bare label behind it.
     */
    static std::string JoinNonEmpty(const std::vector<std::string>& parts, const std::string& delimiter);
    static bool StartsWith(const std::string& str, const std::string& prefix);
    static bool ContainsIgnoreCase(const std::string& str, const std::string& substr);
    static std::string ReplaceAll(const std::string& str, const std::string& from, const std::string& to);

    /** Replace each `{key}` occurrence in @p text with its mapped value. */
    static std::string SubstituteTokens(std::string text, const std::map<std::string, std::string>& tokens);

    /** Escape `& < > " '` as HTML entities for safe embedding in center-HTML panels. */
    static std::string EscapeHtml(const std::string& text);

    /**
     * Truncate to at most `maxBytes` bytes plus `ellipsis`, cutting at a UTF-8 sequence
     * boundary so multibyte text (e.g. Cyrillic) never renders a split character.
     */
    static std::string TruncateUtf8(const std::string& text, std::size_t maxBytes, std::string_view ellipsis = "...");

    static bool IsNumeric(const std::string& str);

    /** Row display text: the (UTF-8-safely truncated) name, or @p id as text when unnamed. */
    static std::string DisplayNameOr(int64_t id, const std::string& name, std::size_t maxBytes = 20);
};

}  // namespace VoltMod
