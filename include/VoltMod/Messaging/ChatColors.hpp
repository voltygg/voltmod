#pragma once

#include <array>
#include <string>
#include <string_view>

namespace VoltMod::ChatColors
{

// CS2 in-line chat color escape bytes. Byte values mirror the SwiftlyS2 mapping
// (references/swiftlys2/src/api/shared/string.cpp). Aliases share a byte.
inline constexpr std::string_view Default = "\x01";
inline constexpr std::string_view White = "\x01";
inline constexpr std::string_view DarkRed = "\x02";
inline constexpr std::string_view LightPurple = "\x03";
inline constexpr std::string_view Green = "\x04";
inline constexpr std::string_view Olive = "\x05";
inline constexpr std::string_view Lime = "\x06";
inline constexpr std::string_view Red = "\x07";
inline constexpr std::string_view Gray = "\x08";
inline constexpr std::string_view Grey = "\x08";
inline constexpr std::string_view Yellow = "\x09";
inline constexpr std::string_view LightYellow = "\x09";
inline constexpr std::string_view Silver = "\x0A";
inline constexpr std::string_view BlueGrey = "\x0A";
inline constexpr std::string_view LightBlue = "\x0B";
inline constexpr std::string_view Blue = "\x0B";
inline constexpr std::string_view DarkBlue = "\x0C";
inline constexpr std::string_view Purple = "\x0E";
inline constexpr std::string_view Magenta = "\x0E";
inline constexpr std::string_view LightRed = "\x0F";
inline constexpr std::string_view Gold = "\x10";
inline constexpr std::string_view Orange = "\x10";

/** A single entry in @ref Palette: the canonical (lowercase) name and its escape code. */
struct NamedColor
{
    std::string_view Name;
    std::string_view Code;
};

/**
 * Canonical, deduplicated list of every named color recognized by @ref ParseNamed,
 * one entry per distinct escape byte. Aliases (e.g. `grey`/`gray`, `magenta`/`purple`)
 * collapse to a single canonical name. Useful for building color-picker UIs that
 * stay in sync with the framework as new colors are added.
 */
inline constexpr std::array<NamedColor, 15> Palette = {{
    {"default", Default},
    {"darkred", DarkRed},
    {"lightpurple", LightPurple},
    {"green", Green},
    {"olive", Olive},
    {"lime", Lime},
    {"red", Red},
    {"gray", Gray},
    {"yellow", Yellow},
    {"silver", Silver},
    {"lightblue", LightBlue},
    {"darkblue", DarkBlue},
    {"purple", Purple},
    {"lightred", LightRed},
    {"gold", Gold},
}};

/**
 * Map a human color name (case-insensitive) to its escape sequence.
 * Returns `Default` for unknown names. Empty input also returns `Default`.
 */
std::string_view ParseNamed(std::string_view name);

/** Strip all CS2 color escape bytes (0x01..0x10) from `text` for safe console/log output. */
std::string Strip(std::string_view text);

}  // namespace VoltMod::ChatColors
