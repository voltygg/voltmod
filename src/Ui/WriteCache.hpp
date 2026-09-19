#pragma once

#include <VoltMod/Core/Slots/PerSlot.hpp>
#include <VoltMod/Core/Slots/Slot.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace VoltMod
{

/** Keeps a text and a class of the same name on the same element apart. */
enum class WriteKind
{
    Text,
    Class
};

/**
 * @brief What each player, or everyone, was last sent on one screen, so an unchanged write is skipped.
 *
 * A networked layout stays on screen without re-sending, so this turns a full redraw into writes
 * for only what changed. SDK-free for its tests.
 */
class WriteCache
{
public:
    /** True when @p value differs from what @p slot (or @ref EveryoneSlot) was last sent, remembering it. */
    bool Changed(int slot, WriteKind kind, std::string_view elementId, std::string_view name, std::string_view value);

    /** True when @p shown differs from the cursor state @p slot was last sent, remembering it. */
    bool CursorChanged(int slot, bool shown);

    /** True the first time it is asked for @p slot, so a failure that repeats every frame logs once. */
    bool IsFirstFailure(int slot);

    /** After a failed write, so the next one goes through. The failure still logs once. */
    void Invalidate(int slot);

    /** Another player took @p slot. */
    void RemoveSlot(int slot);

    /** A new entity has been told nothing. */
    void Clear();

private:
    struct Sent
    {
        std::unordered_map<std::string, std::string> Values;
        std::optional<bool> Cursor;
        bool Failed = false;
    };

    /** @p slot's record, made on first use; the shared one for @ref EveryoneSlot; null for neither. */
    Sent* For(int slot);

    /** (kind, element, name) joined into one key, in a buffer reused across calls. */
    const std::string& KeyFor(WriteKind kind, std::string_view elementId, std::string_view name);

    std::string _key;
    /** Made on first use: a player screen only ever writes for its owner. */
    PerSlot<std::optional<Sent>> _slots;
    Sent _everyone;
};

}  // namespace VoltMod
