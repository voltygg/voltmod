#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

namespace VoltMod
{

/**
 * @brief The sections `gamedata.jsonc` has.
 *
 * Flags, because a member may accept more than one: an address binds from a function or a global.
 */
enum class GameDataSection : uint32_t
{
    Function = 1u << 0,
    Global = 1u << 1,
    VTable = 1u << 2,
    Offset = 1u << 3,
};

constexpr GameDataSection operator|(GameDataSection left, GameDataSection right)
{
    return static_cast<GameDataSection>(static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
}

/** What a lookup holds for one gamedata key. */
struct GameDataLocation
{
    bool Found = false;
    /** A function or global address, or the class table a vtable slot counts in. */
    void* Address = nullptr;
    /** A vtable slot index or a byte offset. -1 when unbound. */
    int Value = -1;
    /** Why it did not bind. Borrowed for the call only; copy it to keep it. Empty when @ref Found. */
    std::string_view Reason;
};

/**
 * Reads what gamedata holds for @p name, when @p sections admits the section holding it.
 *
 * Injected so binding stays out of the host's headers and is unit-tested against a plain lambda,
 * the way @ref OriginalSlotLookup keeps vtable scanning out of Engine. There is still one way to
 * bind gamedata: the app hands @ref Bindings::Bind the process-wide host's lookup.
 */
using GameDataLookup = std::function<GameDataLocation(GameDataSection sections, std::string_view name)>;

}  // namespace VoltMod
