#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace VoltMod::Schema
{

/** Where the generator found one field the accessors use. */
struct FieldLayout
{
    std::string_view Class;
    std::string_view Field;
    int32_t Offset = 0;
    int32_t Size = 0;
};

/** Every field baked into the generated accessors, owner links included. */
std::span<const FieldLayout> GeneratedLayout();

/** The game build represented by @ref GeneratedLayout. */
std::string_view GeneratedFromBuild();

/**
 * @brief A hash of everything @ref GeneratedLayout holds.
 *
 * The host verifies its own copy of the layout, and a plugin may only take that answer when it
 * was built from the same one. Both sides compare this; different values mean the plugin is
 * built against other offsets and must be rebuilt.
 */
uint64_t GeneratedLayoutStamp();

}  // namespace VoltMod::Schema
