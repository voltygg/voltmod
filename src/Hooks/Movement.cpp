#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/MetamodGlobals.hpp>
#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Hooks/Movement.hpp>
#include <VoltMod/Unsafe/VtableHook.hpp>
#include <algorithm>
#include <cs_usercmd.pb.h>
#include <utility>

namespace VoltMod
{

// void* return/param stand in for the real CPlayer_MovementServices::RunCommand(CUserCmd*)
// signature - a pre/post observer never touches either. Bound to the class vtable (DVP hook), so
// it fires for every player without needing a live instance to bind to.
VOLTMOD_VHOOK1(VoltMod_MovementRunCommand, void*, void*);

// The five events share one install: whichever is subscribed to first binds the vtable, and the
// last subscription to drop across all of them unbinds it.
Movement::Movement(EntitySystem& entities, const Bindings& bindings)
    : Pre({.OnFirst = [this] { return Acquire(); }, .OnLast = [this] { ReleaseRef(); }}),
      Post({.OnFirst = [this] { return Acquire(); }, .OnLast = [this] { ReleaseRef(); }}),
      PreCmd({.OnFirst = [this] { return Acquire(); }, .OnLast = [this] { ReleaseRef(); }}),
      PostCmd({.OnFirst = [this] { return Acquire(); }, .OnLast = [this] { ReleaseRef(); }}),
      FilterCmd({.OnFirst = [this] { return Acquire(); }, .OnLast = [this] { ReleaseRef(); }}),
      _entities(entities),
      _bindings(bindings)
{}

Movement::~Movement()
{
    // A Subscription that outlives this service would leave the vtable hook live across a
    // meta reload, calling a handler in an unloaded module.
    if (_refs != 0)
        Log::Error("Movement: {} subscription(s) outlived the hook; a movement handler may dangle.", _refs);
}

bool Movement::Acquire()
{
    if (_refs == 0)
    {
        if (!_bindings.UserCmdPB)
            Log::Warn("Movement: no usable 'UserCmdPB' offset; cmd listeners get Valid=false views.");

        if (!_bindings.UserCmdNumber)
            Log::Warn(
                "Movement: no usable 'UserCmdNumber' offset; falling back to the protobuf's "
                "legacy_command_number, which the live client leaves at 0.");

        _movementServices.fill(nullptr);

        auto hook = VtableHook::OnVTable<VoltMod_MovementRunCommandHook>(
            "Movement RunCommand", _bindings.MovementServices, _bindings.RunCommand.Index(), this,
            &Movement::Hook_RunCommandPre, &Movement::Hook_RunCommandPost, LiveMovementServices());
        if (!hook)
        {
            Log::Warn("Movement: {}; movement handlers will not fire.", hook.error().Detail);
            return false;
        }
        _hook = std::move(*hook);
    }
    ++_refs;
    return true;
}

void Movement::ReleaseRef()
{
    if (_refs > 0 && --_refs == 0)
    {
        _hook.Reset();
        // Every cached pointer belongs to a pawn that may be freed before the next install.
        _movementServices.fill(nullptr);
    }
}

void* Movement::LiveMovementServices() const
{
    for (int slot = 0; slot < MaxPlayers; ++slot)
        if (void* instance = _entities.MovementServices(slot))
            return instance;
    return nullptr;
}

int Movement::SlotFromMovementServices(void* movementServices) const
{
    if (!movementServices)
        return -1;

    // Cache this hot lookup, but confirm each hit so a recycled pointer cannot map
    // to the wrong slot. A stale hit falls through to the rescan.
    auto& entities = _entities;
    for (int slot = 0; slot < MaxPlayers; ++slot)
        if (_movementServices[slot] == movementServices && entities.MovementServices(slot) == movementServices)
            return slot;

    int found = -1;
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        _movementServices[slot] = entities.MovementServices(slot);
        if (_movementServices[slot] == movementServices)
            found = slot;
    }
    return found;
}

void Movement::DecodeUserCmd(void* userCmd)
{
    _cmdView = {};
    if (!userCmd || !_bindings.UserCmdPB)
        return;

    const auto* pb = static_cast<const CSGOUserCmdPB*>(_bindings.UserCmdPB.Ptr(userCmd));
    const auto& base = pb->base();

    _cmdView.Valid = true;
    _cmdView.ClientTick = base.client_tick();
    // The counter the engine maintains lives in the CUserCmd wrapper, not the payload:
    // legacy_command_number stays 0 on a live client, so it is only a fallback.
    if (_bindings.UserCmdNumber)
        _cmdView.CommandNumber = _bindings.UserCmdNumber.Read(userCmd);
    else
        _cmdView.CommandNumber = base.legacy_command_number();
    _cmdView.HasViewAngles = base.has_viewangles();
    if (_cmdView.HasViewAngles)
    {
        _cmdView.ViewPitch = base.viewangles().x();
        _cmdView.ViewYaw = base.viewangles().y();
        _cmdView.ViewRoll = base.viewangles().z();
    }
    _cmdView.ForwardMove = base.forwardmove();
    _cmdView.LeftMove = base.leftmove();
    if (base.has_buttons_pb())
    {
        _cmdView.ButtonsHeld = base.buttons_pb().buttonstate1();
        _cmdView.ButtonsChanged = base.buttons_pb().buttonstate2();
    }
    _cmdView.MouseDx = base.mousedx();
    _cmdView.MouseDy = base.mousedy();
    _cmdView.Attack1StartHistoryIndex = pb->attack1_start_history_index();
    _cmdView.Attack2StartHistoryIndex = pb->attack2_start_history_index();

    int count = std::min(base.subtick_moves_size(), UserCmdView::MaxSubtickMoves);
    _cmdView.SubtickMoveCount = count;
    for (int i = 0; i < count; ++i)
    {
        const auto& move = base.subtick_moves(i);
        _cmdView.SubtickMoves[i] = {
            .Button = move.button(),
            .Pressed = move.pressed(),
            .When = move.when(),
            .PitchDelta = move.pitch_delta(),
            .YawDelta = move.yaw_delta(),
        };
    }

    _cmdView.InputHistoryTotalCount = pb->input_history_size();
    int history = std::min(_cmdView.InputHistoryTotalCount, UserCmdView::MaxInputHistory);
    _cmdView.InputHistorySampleCount = history;
    for (int i = 0; i < history; ++i)
    {
        const auto& entry = pb->input_history(i);
        auto& sample = _cmdView.InputHistorySamples[i];
        sample.TargetEntIndex = entry.target_ent_index();
        if (entry.has_view_angles())
        {
            sample.HasViewAngles = true;
            sample.ViewPitch = entry.view_angles().x();
            sample.ViewYaw = entry.view_angles().y();
        }
    }
}

void* Movement::Hook_RunCommandPre(void* userCmd)
{
    _preSlot = SlotFromMovementServices(META_IFACEPTR(void));
    if (!PreCmd.Empty() || !PostCmd.Empty() || !FilterCmd.Empty())
        DecodeUserCmd(userCmd);
    // Filters edit the decoded view before anyone reads it, so pre/preCmd/postCmd handlers
    // and InputHistory all observe the same edited command.
    FilterCmd.Raise(_preSlot, _cmdView);
    Pre.Raise(_preSlot);
    PreCmd.Raise(_preSlot, _cmdView);
    RETURN_META_VALUE(MRES_IGNORED, nullptr);
}

void* Movement::Hook_RunCommandPost(void* /*userCmd*/)
{
    // Post always brackets the same RunCommand call as the preceding pre (movement is
    // processed one player at a time, no nesting), so reuse the pre-resolved slot and
    // the pre-decoded cmd view rather than repeating the work.
    Post.Raise(_preSlot);
    PostCmd.Raise(_preSlot, _cmdView);
    RETURN_META_VALUE(MRES_IGNORED, nullptr);
}

}  // namespace VoltMod
