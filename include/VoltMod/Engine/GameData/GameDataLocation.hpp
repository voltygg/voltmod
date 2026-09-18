#pragma once

#include <cstdint>
#include <string_view>

namespace VoltMod
{

/** The sections of `gamedata.jsonc`, as flags: an address binds from a function or a global. */
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

/** What gamedata holds for one key. */
struct GameDataLocation
{
    bool Found = false;
    /** A function or global address, or the class table a vtable slot counts in. */
    void* Address = nullptr;
    /** A vtable slot index or a byte offset. -1 when unbound. */
    int Value = -1;
    /** Why it did not bind. Borrowed until the next lookup; copy it to keep it. Empty when @ref Found. */
    std::string_view Reason;
};

}  // namespace VoltMod
