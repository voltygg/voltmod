#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slots/Slot.hpp>
#include <VoltMod/Engine/Detours.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Hooks/Teleport.hpp>
#include <VoltMod/Unsafe/Hook.hpp>
#include <mathlib/vector.h>
#include <utility>

namespace VoltMod
{

Teleport::Teleport(EntitySystem& entities, const Bindings& bindings)
    : Teleported({.OnFirst = [this] { return Install(); }, .OnLast = [this] { _hook.Reset(); }}),
      _entities(entities),
      _bindings(bindings)
{}

Teleport::~Teleport()
{
    // A surviving subscription would call into an unloaded module after meta reload.
    if (!Teleported.Empty())
    {
        Log::Error("Teleport: {} subscription(s) outlived the tracker; a handler may dangle.", Teleported.Count());
    }
}

bool Teleport::Install()
{
    auto hook = HookVirtual("Teleport", _bindings.Teleport,
                            [this](CEntityInstance& pawn, const Vector*, const QAngle*, const Vector*) {
                                // Resolve the slot through the controller so recycled pawn addresses cannot misidentify
                                // it.
                                Teleported.Raise(Pawn{_entities, &pawn}.Slot());
                            });
    if (!hook)
    {
        Log::Warn("Teleport: {}; teleports will not be tracked.", hook.error().Detail);
        return false;
    }

    _hook = std::move(*hook);
    return true;
}

Status Teleport::Available() const
{
    if (!_bindings.Teleport)
    {
        return std::unexpected(Error::Unsupported("the CBaseEntity::Teleport vtable slot did not bind"));
    }
    return {};
}

}  // namespace VoltMod
