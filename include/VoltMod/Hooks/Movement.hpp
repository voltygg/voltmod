#pragma once

#include <VoltMod/Core/Capabilities.hpp>
#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/SharedLifecycle.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Hooks/PlayerInput.hpp>
#include <VoltMod/Unsafe/VtableHook.hpp>

namespace VoltMod
{

/**
 * @brief Vtable hook on CCSPlayer_MovementServices::RunCommand - the per-tick, per-player
 * movement entry point (gamedata vtable "RunCommand").
 *
 * The class vtable is located by RTTI on Windows and by ELF symbol on Linux, so the hook installs
 * with no player connected and covers every player from then on. The first subscription to any
 * of the three events installs it; dropping the last one removes it.
 *
 * Every event carries the owning slot (-1 when unresolved) and the command decoded from the
 * CSGOUserCmdPB payload (gamedata offset "UserCmdPB"). The command is decoded once per
 * RunCommand; its Valid flag is false when the offset is missing or the pointer is null.
 *
 * The vtable index drifts with CS2 updates. A wrong index calls an unrelated vfunc and crashes,
 * and a wrong class name silently resolves nothing, so both are checked at install and a live
 * pawn that disagrees produces a warning.
 */
class Movement
{
public:
    /** @p entities resolves the owning slot, @p bindings the vtable and the byte offsets, and
     *  @p capabilities records a failed install so `Capabilities::Has(Capability::Movement)`
     *  cannot claim a hook that is not there. All three must outlive this hook. */
    Movement(EntitySystem& entities, const Bindings& bindings, Capabilities& capabilities);
    ~Movement();
    Movement(const Movement&) = delete;
    Movement& operator=(const Movement&) = delete;

private:
    /** Declared before the events: all three take a lifecycle from it. */
    SharedLifecycle _lifecycle;

public:
    /** Edit the decoded command before any Before handler reads it. Only the snapshot changes;
     *  the usercmd the engine processes is untouched. For tests and diagnostics. */
    Event<int, PlayerInput&> Rewrite;
    /** Before this player's movement runs. */
    Event<int, const PlayerInput&> Before;
    /** After it ran, with the same command. Where a Before-time state change is restored. */
    Event<int, const PlayerInput&> After;

private:
    bool StartHook();
    void StopHook();

    /** Any connected player's movement services, for the install-time vtable cross-check. */
    void* LiveMovementServices();

    /** Slot whose pawn owns @p movementServices, or -1. */
    int SlotOf(void* movementServices);

    void* Hook_RunCommandPre(void* userCmd);
    void* Hook_RunCommandPost(void* userCmd);
    void Decode(const void* userCmd);

    EntitySystem& _entities;
    Capabilities& _capabilities;
    const Bindings& _bindings;
    VtableHook _hook;
    PlayerInput _cmd;  // decoded in the pre hook, reused by the post hook
    int _slot = -1;    // resolved in the pre hook; RunCommand does not nest
};

}  // namespace VoltMod
