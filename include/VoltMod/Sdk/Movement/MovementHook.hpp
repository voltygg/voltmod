#pragma once

#include <VoltMod/Core/CallbackRegistry.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Sdk/Movement/UserCmd.hpp>
#include <array>
#include <cstdint>
#include <functional>

namespace VoltMod::Sdk
{

/**
 * @brief Manual vtable hook on CCSPlayer_MovementServices::RunCommand - the per-tick,
 * per-player movement entry point (gamedata offset "RunCommand").
 *
 * The hook binds the class vtable, located by RTTI on Windows and by ELF symbol on Linux, so
 * Install() works from OnLoad with no player connected and covers every player from then on.
 *
 * Pre/post callbacks fire around each player's RunCommand with the owning slot resolved
 * (-1 when unresolved). A pre/post pair brackets exactly that player's movement
 * processing, which makes it the place for per-player state flips (see RawConVar).
 *
 * Cmd listeners (ListenPreCmd/ListenPostCmd) additionally receive a UserCmdView - the
 * command's viewangles, buttons, mouse deltas, and sub-tick moves decoded from the
 * CSGOUserCmdPB payload (gamedata byte offset "UserCmdPB" inside the CUserCmd wrapper).
 * The view is decoded once per RunCommand and shared by the pre and post dispatch;
 * when the offset is missing or the pointer is null its Valid flag is false.
 *
 * Filter listeners (ListenFilterCmd) get mutable access to that view and run once, before every
 * pre/preCmd/postCmd listener. They edit only the decoded snapshot (every downstream reader,
 * InputHistory included, sees the edit); the usercmd the engine processes is untouched, since the
 * hook still returns MRES_IGNORED. Intended for test/diagnostic input synthesis only.
 *
 * The vtable index is gamedata-maintained and drifts with CS2 updates; a wrong index
 * calls an unrelated vfunc and crashes, so re-verify it after every update. The class name
 * drifts the same way - a wrong one silently resolves nothing, or another class's table -
 * so both are checked at the same time; Install() warns when a live pawn disagrees with it.
 */
class MovementHook
{
public:
    using Callback = std::function<void(int slot)>;
    using CmdCallback = std::function<void(int slot, const UserCmdView& cmd)>;
    using CmdFilter = std::function<void(int slot, UserCmdView& cmd)>;

    MovementHook() = default;
    ~MovementHook() { Remove(); }
    MovementHook(const MovementHook&) = delete;
    MovementHook& operator=(const MovementHook&) = delete;

    /**
     * Install the class-vtable hook; safe to call from OnLoad and a no-op once installed.
     * False when the gamedata index or the movement-services class cannot be resolved.
     */
    bool Install();
    void Remove();

    [[nodiscard]] Core::Subscription ListenPre(Callback callback)
    {
        return Own(_pre.Add(std::move(callback), _nextId++));
    }
    [[nodiscard]] Core::Subscription ListenPost(Callback callback)
    {
        return Own(_post.Add(std::move(callback), _nextId++));
    }
    [[nodiscard]] Core::Subscription ListenPreCmd(CmdCallback callback)
    {
        return Own(_preCmd.Add(std::move(callback), _nextId++));
    }
    [[nodiscard]] Core::Subscription ListenPostCmd(CmdCallback callback)
    {
        return Own(_postCmd.Add(std::move(callback), _nextId++));
    }

    /** Mutable pre-decode-time edit of the shared UserCmdView; see the class docs. */
    [[nodiscard]] Core::Subscription ListenFilterCmd(CmdFilter filter)
    {
        return Own(_filter.Add(std::move(filter), _nextId++));
    }
    /** Slot whose pawn owns @p movementServices, or -1. */
    int SlotFromMovementServices(void* movementServices) const;

private:
    /** Wrap a freshly issued handle so the caller owns its removal. */
    Core::Subscription Own(uint64_t id)
    {
        return Core::Subscription([this, id] { RemoveListener(id); });
    }

    void RemoveListener(uint64_t id);

    void* Hook_RunCommandPre(void* userCmd);
    void* Hook_RunCommandPost(void* userCmd);
    void DecodeUserCmd(void* userCmd);

    Core::CallbackRegistry<Callback> _pre;
    Core::CallbackRegistry<Callback> _post;
    Core::CallbackRegistry<CmdCallback> _preCmd;
    Core::CallbackRegistry<CmdCallback> _postCmd;
    Core::CallbackRegistry<CmdFilter> _filter;
    uint64_t _nextId = 1;       // one handle space across all five registries, so RemoveListener is unambiguous
    UserCmdView _cmdView;       // decoded once per RunCommand, reused across pre/post dispatch
    int _pbOffset = -1;         // gamedata "UserCmdPB"; negative disables decoding
    int _cmdNumberOffset = -1;  // gamedata "UserCmdNumber"; negative falls back to legacy_command_number
    bool _installed = false;
    int _preHookId = 0;  // SourceHook DVP-hook ids; removal is by id, see Remove()
    int _postHookId = 0;
    int _preSlot = -1;  // slot resolved in the pre hook, reused by the immediately-following post
    /** Slot -> movement services, to keep the per-usercmd slot lookup off the entity system.
     *  A hit is still confirmed against the engine, so a stale entry can only cost a rescan. */
    mutable std::array<void*, Core::MaxPlayers> _movementServices{};
};

}  // namespace VoltMod::Sdk
