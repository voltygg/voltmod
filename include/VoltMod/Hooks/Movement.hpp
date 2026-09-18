#pragma once

#include <VoltMod/Core/Signals/Event.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Signals/SharedLifecycle.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Hooks/PlayerInput.hpp>
#include <VoltMod/Core/Signals/Subscription.hpp>

namespace VoltMod
{

/**
 * @brief Per-player movement events from CCSPlayer_MovementServices::RunCommand.
 *
 * The class vtable is located by RTTI on Windows and by ELF symbol on Linux. The hook therefore
 * covers every player, including players who connect after installation, and remains installed
 * only while at least one event has a subscriber.
 *
 * Each event carries the owning slot and a command decoded once per RunCommand from the
 * CSGOUserCmdPB payload. The slot is -1 when unresolved, and PlayerInput::Valid is false when the
 * gamedata offset or payload pointer is unavailable.
 *
 * The vtable index and class name must match the running game. A mismatched index can call an
 * unrelated function and crash; a mismatched class prevents installation. A live pawn with a
 * different table produces a warning.
 */
class Movement
{
public:
    /** @p entities resolves the owning slot and @p bindings the vtable and the byte offsets. Both
     *  must outlive this hook. */
    Movement(EntitySystem& entities, const Bindings& bindings);
    ~Movement();
    Movement(const Movement&) = delete;
    Movement& operator=(const Movement&) = delete;

private:
    SharedLifecycle _lifecycle;

public:
    /** Edit the decoded command seen by handlers. The engine's usercmd is unchanged. */
    Event<int, PlayerInput&> Rewrite;
    Event<int, const PlayerInput&> Before;
    Event<int, const PlayerInput&> After;

    /** Why movement events cannot fire: the RunCommand slot or the usercmd offset did not bind. */
    Status Available() const;

private:
    bool Install();

    /** Slot whose pawn owns @p movementServices, or -1. */
    int SlotOf(void* movementServices);

    void Decode(const void* userCmd);

    EntitySystem& _entities;
    const Bindings& _bindings;
    Subscription _hook;
    PlayerInput _cmd;  // decoded in the pre hook, reused by the post hook
    int _slot = -1;    // resolved in the pre hook; RunCommand does not nest
};

}  // namespace VoltMod
