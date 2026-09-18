#pragma once

#include "Ui/LayoutPath.hpp"
#include "Ui/WriteCache.hpp"

#include <VoltMod/Core/Results/Result.hpp>
#include <VoltMod/Core/Slots/Slot.hpp>
#include <VoltMod/Core/Slots/SlotEvents.hpp>
#include <VoltMod/Core/Signals/Subscription.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Entities/EntityOps.hpp>
#include <VoltMod/Entities/EntityRef.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Hooks/Visibility.hpp>
#include <string_view>

namespace VoltMod
{

/**
 * @brief The `custom_hud_layout` entity behind a @ref Screen: spawning, removal, and every write.
 *
 * A shared screen writes the layout's global state (@ref EveryoneSlot) or one player's state. The
 * client shows per-player state for the pawn it is *watching*, so a player screen instead writes
 * text and classes to the global state of an entity only its owner receives, and writes the cursor
 * to the owner's own slot. That is what keeps a menu working while its owner is dead or spectating.
 *
 * Writes never spawn; @ref EnsureSpawned does. Unchanged values are skipped through @ref WriteCache.
 */
class ScreenEntity
{
public:
    /** Every reference must outlive this. @p owner is a player slot, or @ref EveryoneSlot for a shared screen. */
    ScreenEntity(EntitySystem& entities, EntityOps& ops, SlotEvents& slots, Visibility& visibility, LayoutPath layout,
                 int owner);
    ~ScreenEntity();

    ScreenEntity(const ScreenEntity&) = delete;
    ScreenEntity& operator=(const ScreenEntity&) = delete;

    [[nodiscard]] bool Exists() const;

    /** Spawn, or respawn after the player list changed, until the entity covers @p slot. */
    bool EnsureSpawned(int slot);

    /** Remove the entity and forget everything written to it. Safe to call again. */
    void Remove();

    Status WriteText(int slot, std::string_view variable, std::string_view value);
    Status WriteClass(int slot, std::string_view elementId, std::string_view className, bool on);
    Status WriteCursor(int slot, bool shown);

private:
    [[nodiscard]] bool IsForPlayer() const noexcept { return _owner != EveryoneSlot; }

    /** The slot a write is cached under, which the cursor is also sent to. A player screen accepts
     *  only its owner or @ref EveryoneSlot. */
    [[nodiscard]] Result<int> CacheSlotFor(int slot) const;

    /** The slot text and classes are sent to: global on a player screen. */
    [[nodiscard]] int ContentSlotFor(int cacheSlot) const noexcept;

    Status Spawn();
    bool SpawnOrWarn();

    [[nodiscard]] int PlayerStateCount() const;
    [[nodiscard]] bool Covers(int slot) const;

    /** The entity, or why @p engineSlot cannot be written to right now. */
    [[nodiscard]] Result<CEntityInstance*> EntityForWrite(int engineSlot) const;

    Status SendText(int engineSlot, std::string_view variable, std::string_view value);
    Status SendClass(int engineSlot, std::string_view elementId, std::string_view className, bool on);
    Status SendCursor(int engineSlot, bool shown);

    /** Pass @p status on; a failed per-slot write is forgotten so the next redraw retries, logged once. */
    Status Record(int cacheSlot, Status status, std::string_view what);

    EntitySystem& _entities;
    EntityOps& _ops;
    Visibility& _visibility;
    LayoutPath _layout;
    int _owner;

    EntityRef _entity;
    /** Per-player state is sized at spawn, so a later slot is only reachable through a new entity. */
    bool _playersChangedSinceSpawn = true;
    WriteCache _written;

    /** Declared last: its handler touches the members above. */
    Subscription _slotChanges;
};

}  // namespace VoltMod
