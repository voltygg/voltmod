#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace VoltMod::Schema
{

/** A field layout emitted by the generator. */
struct FieldLayout
{
    std::string_view Name;
    int32_t Offset = 0;
    int32_t Size = 0;
};

/** A class layout and the fields used by generated accessors. */
struct ClassLayout
{
    std::string_view Name;
    int32_t Size = 0;
    int32_t OwnerLinkOffset = -1;
    std::span<const FieldLayout> Fields;
};

/** The layout used to build generated accessors. */
std::span<const ClassLayout> GeneratedLayout();

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
