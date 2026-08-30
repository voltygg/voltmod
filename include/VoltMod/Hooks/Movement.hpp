#pragma once

#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Hooks/UserCmd.hpp>
#include <VoltMod/Unsafe/VtableHook.hpp>
#include <array>
#include <cstdint>

namespace VoltMod
{

/**
 * @brief Manual vtable hook on CCSPlayer_MovementServices::RunCommand - the per-tick,
 * per-player movement entry point (gamedata offset "RunCommand").
 *
 * The hook binds the class vtable, located by RTTI on Windows and by ELF symbol on Linux, so it
 * can be installed from OnLoad with no player connected and covers every player from then on.
 * Installing is not a separate call: the first subscription to any of the four events below
 * installs it, and dropping the last one removes it.
 *
 * @ref Pre and @ref Post fire around each player's RunCommand with the owning slot resolved
 * (-1 when unresolved). A pre/post pair brackets exactly that player's movement processing,
 * which makes it the place for per-player state flips (see ConVar::RawScope).
 *
 * @ref PreCmd additionally receives a UserCmdView - the command's viewangles, buttons, mouse
 * deltas, and sub-tick moves decoded from the CSGOUserCmdPB payload (gamedata byte offset
 * "UserCmdPB" inside the CUserCmd wrapper). The view is decoded once per RunCommand; when the
 * offset is missing or the pointer is null its Valid flag is false.
 *
 * @ref FilterCmd gets mutable access to that view and runs once, before every other handler. It
 * edits only the decoded snapshot (every downstream reader sees the edit); the usercmd the
 * engine processes is untouched, since the hook still returns MRES_IGNORED.
 * Intended for test/diagnostic input synthesis only.
 *
 * The vtable index is gamedata-maintained and drifts with CS2 updates; a wrong index calls an
 * unrelated vfunc and crashes, so re-verify it after every update. The class name drifts the same
 * way - a wrong one silently resolves nothing, or another class's table - so both are checked at
 * the same time, and a live pawn that disagrees produces a warning at install.
 */
class Movement
{
public:
    /** @p entities resolves the owning slot per usercmd, @p bindings the vtable index, the class
     *  vtable and the byte offsets. Both must outlive this hook; the Runtime declares them above. */
    Movement(EntitySystem& entities, const Bindings& bindings);
    ~Movement();
    Movement(const Movement&) = delete;
    Movement& operator=(const Movement&) = delete;

    /** Before this player's movement runs. */
    Event<int> Pre;
    /** After it ran - where a Pre-time state flip is restored. */
    Event<int> Post;
    /** Before movement runs, with the decoded command. */
    Event<int, const UserCmdView&> PreCmd;
    /** Mutable edit of the decoded view, before any other handler sees it; see the class docs. */
    Event<int, UserCmdView&> FilterCmd;

    /** Slot whose pawn owns @p movementServices, or -1. */
    int SlotFromMovementServices(void* movementServices) const;

private:
    /** One shared install behind four events: the first subscription across all of them binds the
     *  vtable and the last one to drop unbinds it. */
    bool OnFirstSubscriber();
    void OnLastSubscriber();

    /** Any connected player's movement services, for the install-time vtable cross-check. */
    void* LiveMovementServices() const;

    void* Hook_RunCommandPre(void* userCmd);
    void* Hook_RunCommandPost(void* userCmd);
    void DecodeUserCmd(void* userCmd);

    EntitySystem& _entities;
    const Bindings& _bindings;
    int _subscribers = 0;  // live subscriptions across all four events
    UserCmdView _cmdView;  // decoded once per RunCommand, reused across pre/post dispatch
    VtableHook _hook;      // the pre/post pair; removed by dropping it
    int _preSlot = -1;     // slot resolved in the pre hook, reused by the immediately-following post
    /** Slot -> movement services, to keep the per-usercmd slot lookup off the entity system.
     *  A hit is still confirmed against the engine, so a stale entry can only cost a rescan. */
    mutable std::array<void*, MaxPlayers> _movementServices{};
};

}  // namespace VoltMod
