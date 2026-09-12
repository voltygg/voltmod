#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/MetamodGlobals.hpp>
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
    // A Subscription that outlives this service would leave the hook live across a meta reload,
    // calling a handler in an unloaded module.
    if (!Teleported.Empty())
        Log::Error("Teleport: {} subscription(s) outlived the tracker; a handler may dangle.", Teleported.Count());
}

bool Teleport::Install()
{
    auto hook = HookVTable("Teleport", _bindings.Teleport,
                           [this](HookedPawn& pawn, const Vector*, const QAngle*, const Vector*) {
                               // Resolved per call through the pawn's controller, so a recycled
                               // pawn address cannot report the previous owner's slot.
                               Teleported.Raise(Pawn{_entities, reinterpret_cast<CEntityInstance*>(&pawn)}.Slot());
                           });
    if (!hook)
    {
        Log::Warn("Teleport: {}; teleports will not be tracked.", hook.error().Detail);
        return false;
    }

    _hook = std::move(*hook);
    return true;
}

}  // namespace VoltMod
