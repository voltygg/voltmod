#include "Ui/ScreenEntity.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Entities/KeyValues.hpp>
#include <VoltMod/Schema/Api.hpp>
#include <format>
#include <tier1/utlstring.h>
#include <tier1/utlvector.h>
#include <utility>

namespace VoltMod
{

using StringTable = CUtlVector<CUtlString>;

/** EHudPanelClassStatus_t values. */
static constexpr int32_t ClassAbsent = 0;
static constexpr int32_t ClassPresent = 1;

/** CS2 refuses to intern past this many entries per table; stop short so a runaway caller cannot fill them. */
static constexpr int StringTableCap = 1024;
static constexpr int StringTableHeadroom = 64;

ScreenEntity::ScreenEntity(EntitySystem& entities, EntityOps& ops, SlotEvents& slots, Visibility& visibility,
                           LayoutPath layout, int owner)
    : _entities(entities), _ops(ops), _visibility(visibility), _layout(std::move(layout)), _owner(owner)
{
    _written.BindReset(slots);
    _slotChanges = slots.Changed += [this](int slot) {
        _playersChangedSinceSpawn = true;
        if (slot == _owner)  // a player screen leaves with its owner
            Remove();
    };
}

ScreenEntity::~ScreenEntity()
{
    Remove();
}

bool ScreenEntity::Exists() const
{
    return static_cast<bool>(_entities.Resolve(_entity));
}

bool ScreenEntity::EnsureSpawned(int slot)
{
    if (slot != EveryoneSlot && !IsValidSlot(slot))
        return false;

    const auto target = TargetFor(slot, /*ownSlot=*/true);
    if (!target)
        return false;

    if (!Exists() && !SpawnOrWarn())
        return false;

    if (target->CacheSlot == EveryoneSlot || Covers(target->CacheSlot))
        return true;

    // Respawning for any other reason would retry a hopeless spawn every frame.
    if (!_playersChangedSinceSpawn)
        return false;

    return SpawnOrWarn() && Covers(target->CacheSlot);
}

void ScreenEntity::Remove()
{
    if (IsForPlayer())
        _visibility.ShowToEveryone(_entity);
    if (Entity entity = _entities.Resolve(_entity))
        _ops.Remove(entity.Raw());

    _entity = {};
    _written.ForgetAll();
}

Status ScreenEntity::WriteText(int slot, std::string_view variable, std::string_view value)
{
    const auto target = TargetFor(slot, /*ownSlot=*/false);
    if (!target)
        return std::unexpected(target.error());
    if (!_written.Changed(target->CacheSlot, WriteKind::Text, _layout.Name(), variable, value))
        return {};

    return Record(target->CacheSlot, SendText(target->EngineSlot, variable, value), variable);
}

Status ScreenEntity::WriteClass(int slot, std::string_view elementId, std::string_view className, bool on)
{
    const auto target = TargetFor(slot, /*ownSlot=*/false);
    if (!target)
        return std::unexpected(target.error());
    if (!_written.Changed(target->CacheSlot, WriteKind::Class, elementId, className, on ? "1" : "0"))
        return {};

    return Record(target->CacheSlot, SendClass(target->EngineSlot, elementId, className, on), elementId);
}

Status ScreenEntity::WriteCursor(int slot, bool shown)
{
    const auto target = TargetFor(slot, /*ownSlot=*/true);
    if (!target)
        return std::unexpected(target.error());
    if (!_written.CursorChanged(target->CacheSlot, shown))
        return {};

    return Record(target->CacheSlot, SendCursor(target->EngineSlot, shown), "the cursor");
}

Result<ScreenEntity::Target> ScreenEntity::TargetFor(int slot, bool ownSlot) const
{
    if (!IsForPlayer())
        return Target{.CacheSlot = slot, .EngineSlot = slot};

    if (slot != EveryoneSlot && slot != _owner)
        return std::unexpected(Error::Invalid(std::format("this screen belongs to slot {}", _owner)));

    return Target{.CacheSlot = _owner, .EngineSlot = ownSlot ? _owner : EveryoneSlot};
}

Status ScreenEntity::Spawn()
{
    _playersChangedSinceSpawn = false;
    Remove();

    if (!_ops.CanSpawn())
        return std::unexpected(Error::Unsupported("entity spawning is unavailable"));

    KeyValues values;
    values.Set("layout", _layout.Resource());

    CEntityInstance* spawned = _ops.Spawn("custom_hud_layout", values);
    if (!spawned)
        return std::unexpected(Error::Engine("the engine refused to spawn custom_hud_layout"));

    _entity = Entity(_entities, spawned).Ref();
    if (IsForPlayer())
        _visibility.ShowOnlyTo(_entity, _owner);
    return {};
}

bool ScreenEntity::SpawnOrWarn()
{
    const Status spawned = Spawn();
    if (!spawned)
        Log::Warn("Screen '{}': spawn failed ({}).", _layout.Name(), spawned.error().Detail);
    return spawned.has_value();
}

int ScreenEntity::PlayerStateCount() const
{
    // The embedded vector keeps its count first, which is what the engine itself reads before indexing.
    const Schema::CCSCustomHudLayout layout{_entities.Resolve(_entity).Raw()};
    return layout ? layout.PlayerLayoutStates() : -1;
}

bool ScreenEntity::Covers(int slot) const
{
    if (IsForPlayer() && slot != _owner)
        return false;
    return IsValidSlot(slot) && PlayerStateCount() > slot;
}

Result<CEntityInstance*> ScreenEntity::EntityForWrite(int engineSlot) const
{
    Entity entity = _entities.Resolve(_entity);
    if (!entity)
        return std::unexpected(Error::NotFound("the custom_hud_layout entity no longer exists"));

    const Schema::CCSCustomHudLayout layout{entity.Raw()};
    for (const auto& [name, table] :
         {std::pair{"element id", layout.PanelIds()}, std::pair{"class name", layout.ClassNames()},
          std::pair{"text variable", layout.DialogVariableNames()}})
    {
        const int count = table ? static_cast<const StringTable*>(table)->Count() : -1;
        if (count >= StringTableCap - StringTableHeadroom)
            return std::unexpected(
                Error::Failed(std::format("the {} table is nearly full ({}/{})", name, count, StringTableCap)));
    }

    if (engineSlot == EveryoneSlot)
        return entity.Raw();

    // The engine's per-player setters return silently for a slot past the state count.
    if (const int states = PlayerStateCount(); states <= engineSlot)
        return std::unexpected(Error::Failed(
            std::format("slot {} has no per-player layout state (the entity holds {})", engineSlot, states)));

    return entity.Raw();
}

Status ScreenEntity::SendText(int engineSlot, std::string_view variable, std::string_view value)
{
    auto entity = EntityForWrite(engineSlot);
    if (!entity)
        return std::unexpected(entity.error());

    // Assigned in place: CUtlString allocates inside tier0, the allocator the engine frees with.
    CUtlString root, name, text;
    root.SetDirect(_layout.Name().data(), static_cast<int>(_layout.Name().size()));
    name.SetDirect(variable.data(), static_cast<int>(variable.size()));
    text.SetDirect(value.data(), static_cast<int>(value.size()));

    const Bindings& bindings = _entities.BindingsRef();
    if (engineSlot == EveryoneSlot)
    {
        if (!bindings.CustomHudSetDialogVariable)
            return std::unexpected(Error::Unsupported("the custom HUD text setter did not bind"));
        bindings.CustomHudSetDialogVariable(*entity, &root, &name, &text);
        return {};
    }

    if (!bindings.CustomHudSetDialogVariableForPlayer)
        return std::unexpected(Error::Unsupported("the custom HUD per-player text setter did not bind"));
    bindings.CustomHudSetDialogVariableForPlayer(*entity, engineSlot, &root, &name, &text);
    return {};
}

Status ScreenEntity::SendClass(int engineSlot, std::string_view elementId, std::string_view className, bool on)
{
    auto entity = EntityForWrite(engineSlot);
    if (!entity)
        return std::unexpected(entity.error());

    CUtlString element, name;
    element.SetDirect(elementId.data(), static_cast<int>(elementId.size()));
    name.SetDirect(className.data(), static_cast<int>(className.size()));
    const int32_t state = on ? ClassPresent : ClassAbsent;

    const Bindings& bindings = _entities.BindingsRef();
    if (engineSlot == EveryoneSlot)
    {
        if (!bindings.CustomHudSetHasClass)
            return std::unexpected(Error::Unsupported("the custom HUD class setter did not bind"));
        bindings.CustomHudSetHasClass(*entity, &element, &name, state);
        return {};
    }

    if (!bindings.CustomHudSetHasClassForPlayer)
        return std::unexpected(Error::Unsupported("the custom HUD per-player class setter did not bind"));
    bindings.CustomHudSetHasClassForPlayer(*entity, engineSlot, &element, &name, state);
    return {};
}

Status ScreenEntity::SendCursor(int engineSlot, bool shown)
{
    auto entity = EntityForWrite(engineSlot);
    if (!entity)
        return std::unexpected(entity.error());

    if (engineSlot != EveryoneSlot)
    {
        const auto& set = _entities.BindingsRef().CustomHudSetInputCapture;
        if (!set)
            return std::unexpected(Error::Unsupported("the custom HUD input capture setter did not bind"));
        set(*entity, engineSlot, shown);
        return {};
    }

    // No engine setter takes the global state; this plain bool has no container or index behind it.
    Schema::CCSCustomHudLayout{*entity}.GlobalLayoutState().SetInputCaptureEnabled(shown);
    return {};
}

Status ScreenEntity::Record(int cacheSlot, Status status, std::string_view what)
{
    if (status || !IsValidSlot(cacheSlot))
        return status;

    _written.Forget(cacheSlot);
    if (_written.IsFirstFailure(cacheSlot))
        Log::Warn("Screen '{}': writing {} for slot {} failed ({}).", _layout.Name(), what, cacheSlot,
                  status.error().Detail);
    return status;
}

}  // namespace VoltMod
