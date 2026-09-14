#pragma once

#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
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
 * for only what changed. A slot changing hands forgets that slot. SDK-free for its tests.
 */
class WriteCache
{
public:
    /** Forget a slot when a player joins or leaves it. */
    void BindReset(SlotEvents& slots) { _slots.BindReset(slots); }

    /** True when @p value differs from what @p slot (or @ref EveryoneSlot) was last sent, remembering it. */
    bool Changed(int slot, WriteKind kind, std::string_view elementId, std::string_view name, std::string_view value);

    /** True when @p shown differs from the cursor state @p slot was last sent, remembering it. */
    bool CursorChanged(int slot, bool shown);

    /** True the first time it is asked for @p slot, so a failure that repeats every frame logs once. */
    bool IsFirstFailure(int slot);

    /** Drop what @p slot was sent, so the next write goes through. Keeps the failure flag. */
    void Forget(int slot);

    /** Drop everything, failure flags included: a new entity has been told nothing. */
    void ForgetAll();

private:
    struct Sent
    {
        std::unordered_map<std::string, std::string> Values;
        std::optional<bool> Cursor;
        bool Failed = false;
    };

    /** @p slot's record, the shared one for @ref EveryoneSlot, or null for neither. */
    Sent* For(int slot);

    /** (kind, element, name) joined into one key, in a buffer reused across calls. */
    const std::string& KeyFor(WriteKind kind, std::string_view elementId, std::string_view name);

    std::string _key;
    PerSlot<Sent> _slots;
    Sent _everyone;
};

}  // namespace VoltMod
