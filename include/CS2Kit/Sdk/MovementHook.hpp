#pragma once

#include <CS2Kit/Core/CallbackRegistry.hpp>
#include <CS2Kit/Sdk/UserCmd.hpp>
#include <cstdint>
#include <functional>

namespace CS2Kit::Sdk
{

/**
 * @brief Manual vtable hook on CPlayer_MovementServices::RunCommand - the per-tick,
 * per-player movement entry point (gamedata offset "RunCommand").
 *
 * Install() needs at least one live movement-services instance (i.e. a spawned pawn), so
 * call it lazily - e.g. from a PlayerSpawn listener - and treat false as "retry later".
 * SourceHook patches the shared vtable, so hooking one instance covers every player.
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
 * calls an unrelated vfunc and crashes, so re-verify it after every update.
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

    /** Install the vtable hook. False when the gamedata offset is missing or no pawn is live yet. */
    bool Install();
    bool Installed() const { return _installed; }
    void Remove();

    uint64_t ListenPre(Callback callback) { return _pre.Add(std::move(callback), _nextId++); }
    uint64_t ListenPost(Callback callback) { return _post.Add(std::move(callback), _nextId++); }
    uint64_t ListenPreCmd(CmdCallback callback) { return _preCmd.Add(std::move(callback), _nextId++); }
    uint64_t ListenPostCmd(CmdCallback callback) { return _postCmd.Add(std::move(callback), _nextId++); }

    /** Mutable pre-decode-time edit of the shared UserCmdView; see the class docs. */
    uint64_t ListenFilterCmd(CmdFilter filter) { return _filter.Add(std::move(filter), _nextId++); }
    void RemoveListener(uint64_t id);

    /** Slot whose pawn owns @p movementServices, or -1. */
    int SlotFromMovementServices(void* movementServices) const;

private:
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
    int _preHookId = 0;  // SourceHook VP-hook ids; removal is by id, see Remove()
    int _postHookId = 0;
    int _preSlot = -1;  // slot resolved in the pre hook, reused by the immediately-following post
};

}  // namespace CS2Kit::Sdk
