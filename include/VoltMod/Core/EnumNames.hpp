#pragma once

// Pin the scanned range explicitly rather than inheriting magic_enum's default: the framework's
// enums are small and contiguous, and a wide range costs compile time in every including TU.
// These must be set before magic_enum.hpp is first included, which is why nothing else in the
// framework includes it directly.
#ifndef MAGIC_ENUM_RANGE_MIN
#define MAGIC_ENUM_RANGE_MIN 0
#endif
#ifndef MAGIC_ENUM_RANGE_MAX
#define MAGIC_ENUM_RANGE_MAX 255
#endif

#include <cstddef>
#include <magic_enum/magic_enum.hpp>
#include <optional>
#include <string_view>

namespace VoltMod
{

/**
 * @file EnumNames.hpp
 * @brief Enumerator names without a hand-written switch.
 *
 * Every framework enum spells its enumerators the way the log and the JSON status want to read
 * them, so the name is the compiler's copy of the identifier. A switch that maps them to strings
 * is one more thing to forget when an enumerator is added.
 */

/** Identifier of @p value, or an empty view when it is not a declared enumerator. */
template <class E>
constexpr std::string_view Name(E value) noexcept
{
    return magic_enum::enum_name(value);
}

/**
 * The enumerator spelled @p name, or nullopt.
 *
 * Case-insensitive: enumerators are PascalCase, but the text being parsed is almost always a
 * lowercase config value ("observe", "grants"), and a case-sensitive match would send every
 * such call back to a hand-written chain. Pair it with `value_or` to supply the fallback.
 */
template <class E>
constexpr std::optional<E> Parse(std::string_view name) noexcept
{
    return magic_enum::enum_cast<E>(name, magic_enum::case_insensitive);
}

/** How many enumerators @p E declares. */
template <class E>
inline constexpr std::size_t EnumCount = magic_enum::enum_count<E>();

/** Position of @p value in @p E's declaration order, for indexing a per-enumerator array. */
template <class E>
constexpr std::size_t EnumIndex(E value) noexcept
{
    return magic_enum::enum_index(value).value_or(EnumCount<E>);
}

/** Every enumerator of @p E, in declaration order. */
template <class E>
constexpr auto EnumValues() noexcept
{
    return magic_enum::enum_values<E>();
}

}  // namespace VoltMod
