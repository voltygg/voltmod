#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/** @p text as an int64_t, or nullopt unless it is entirely digits (with an optional sign). */
std::optional<int64_t> ParseInt64(std::string_view text);

/** @p text as a uint64_t, or nullopt unless it is entirely digits. A leading `-` is rejected
 *  rather than wrapped, which is the whole reason this is not @ref ParseInt64 with a cast. */
std::optional<uint64_t> ParseUInt64(std::string_view text);

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
    static std::string ToLower(std::string_view str);
    static std::string Trim(std::string_view str);
    static std::string Join(const std::vector<std::string>& parts, std::string_view delimiter);

    /**
     * Join @p parts with @p delimiter, skipping empty ones.
     *
     * For text assembled from optional pieces - a ban notice's reason, expiry and appeal link,
     * say. Skipping is the point: a piece that is not configured must not leave a dangling
     * delimiter or a bare label behind it.
     */
    static std::string JoinNonEmpty(const std::vector<std::string>& parts, std::string_view delimiter);
    static bool StartsWith(std::string_view str, std::string_view prefix);
    static bool ContainsIgnoreCase(std::string_view str, std::string_view substr);

    /** Replace each `{key}` occurrence in @p text with its mapped value. */
    static std::string SubstituteTokens(std::string text, const std::map<std::string, std::string>& tokens);

    /** Escape `& < > " '` as HTML entities for safe embedding in center-HTML panels. */
    static std::string EscapeHtml(std::string_view text);

    /**
     * @brief Replace every byte that is not part of a well-formed UTF-8 sequence with @p replacement.
     *
     * Rejects lone continuation bytes, truncated sequences, overlong encodings, surrogates and
     * anything above U+10FFFF. Player names arrive from the client and need not be valid UTF-8;
     * a JSON writer passes the bytes straight through, so text bound for a webhook or a panel is
     * sanitized here rather than at each call site. The default replacement is U+FFFD.
     */
    static std::string SanitizeUtf8(std::string_view text, std::string_view replacement = "\xEF\xBF\xBD");

    /**
     * Truncate to at most `maxBytes` bytes plus `ellipsis`, cutting at a UTF-8 sequence
     * boundary so multibyte text (e.g. Cyrillic) never renders a split character. Routes through
     * @ref SanitizeUtf8, so already-malformed input is not handed back verbatim.
     */
    static std::string TruncateUtf8(std::string_view text, std::size_t maxBytes, std::string_view ellipsis = "...");

    static bool IsNumeric(std::string_view str);

    /** Row display text, HTML-escaped: the (UTF-8-safely truncated) name, or @p id when unnamed. */
    static std::string DisplayNameOr(int64_t id, std::string_view name, std::size_t maxBytes = 20);
};

}  // namespace VoltMod
