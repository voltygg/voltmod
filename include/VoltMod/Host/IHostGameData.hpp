#pragma once

#include <VoltMod/Host/HostTypes.hpp>
#include <cstdint>

namespace VoltMod
{

/**
 * @brief The sections `gamedata.jsonc` has.
 *
 * Flags, because a member may accept more than one: an address binds from a function or a global.
 */
enum class GameDataKind : uint32_t
{
    Function = 1u << 0,
    Global = 1u << 1,
    VTable = 1u << 2,
    Offset = 1u << 3,
};

constexpr GameDataKind operator|(GameDataKind left, GameDataKind right)
{
    return static_cast<GameDataKind>(static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
}

/** What the host holds for one gamedata key. */
struct GameDataEntry
{
    bool Found = false;
    /** A function or global address, or the class table a vtable slot counts in. */
    void* Address = nullptr;
    /** A vtable slot index or a byte offset. -1 when unbound. */
    int Value = -1;
    /** Why it did not bind. Owned by the host and valid until the next @ref IHostGameData::Lookup;
     *  copy it to keep it. Empty when @ref Found. */
    HostString Reason;
};

/**
 * @brief The gamedata the host read and resolved once for the whole process.
 *
 * The host scans the game's modules at startup, before any plugin loads, so a signature broken by
 * a game update costs one scan and one log line rather than one per plugin. Each plugin's
 * @ref Bindings is the typed view of this and the only thing that calls it.
 *
 * Game thread only. The entries never change after startup.
 */
struct IHostGameData
{
    static constexpr const char* InterfaceName = "VoltMod.IHostGameData";

    /** What the host resolved for @p name, when @p kinds admits the section holding it. */
    virtual GameDataEntry Lookup(GameDataKind kinds, HostString name) = 0;

protected:
    ~IHostGameData() = default;
};

}  // namespace VoltMod
