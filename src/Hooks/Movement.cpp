#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/MetamodGlobals.hpp>
#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Hooks/Movement.hpp>
#include <VoltMod/Schema/Api.hpp>
#include <VoltMod/Unsafe/VtableHook.hpp>
#include <algorithm>
#include <cs_usercmd.pb.h>
#include <utility>

namespace VoltMod
{

// Hooks CPlayer_MovementServices::RunCommand for every player. The opaque types are unused.
VOLTMOD_VHOOK1(VoltMod_MovementRunCommand, void*, void*);

// All movement events share one hook installation.
Movement::Movement(EntitySystem& entities, const Bindings& bindings)
    : Pre({.OnFirst = [this] { return OnFirstSubscriber(); }, .OnLast = [this] { OnLastSubscriber(); }}),
      Post({.OnFirst = [this] { return OnFirstSubscriber(); }, .OnLast = [this] { OnLastSubscriber(); }}),
      PreCmd({.OnFirst = [this] { return OnFirstSubscriber(); }, .OnLast = [this] { OnLastSubscriber(); }}),
      FilterCmd({.OnFirst = [this] { return OnFirstSubscriber(); }, .OnLast = [this] { OnLastSubscriber(); }}),
      _entities(entities),
      _bindings(bindings)
{}

Movement::~Movement()
{
    // Never leave a hook pointing into an unloaded module.
    if (_subscribers != 0)
        Log::Error("Movement: {} subscription(s) outlived the hook; a movement handler may dangle.", _subscribers);
}

bool Movement::OnFirstSubscriber()
{
    if (_subscribers == 0)
    {
        if (!_bindings.UserCmdPB)
            Log::Warn("Movement: no usable 'UserCmdPB' offset; cmd listeners get Valid=false views.");

        if (!_bindings.UserCmdNumber)
            Log::Warn(
                "Movement: no usable 'UserCmdNumber' offset; falling back to the protobuf's "
                "legacy_command_number, which the live client leaves at 0.");

        _movementServices.fill({});

        auto hook = VtableHook::OnVTable<VoltMod_MovementRunCommandHook>(
            "Movement RunCommand", _bindings.RunCommand, this, &Movement::Hook_RunCommandPre,
            &Movement::Hook_RunCommandPost, LiveMovementServices());
        if (!hook)
        {
            Log::Warn("Movement: {}; movement handlers will not fire.", hook.error().Detail);
            return false;
        }
        _hook = std::move(*hook);
    }
    ++_subscribers;
    return true;
}

void Movement::OnLastSubscriber()
{
    if (_subscribers > 0 && --_subscribers == 0)
    {
        _hook.Reset();
        // Pawn pointers may be stale after reinstall.
        _movementServices.fill({});
    }
}

void* Movement::LiveMovementServices() const
{
    for (int slot = 0; slot < MaxPlayers; ++slot)
        if (Schema::CPlayer_MovementServices instance = _entities.MovementServices(slot))
            return instance.Base();
    return nullptr;
}

int Movement::SlotFromMovementServices(void* movementServices) const
{
    if (!movementServices)
        return -1;

    // Validate cached pointers before reuse.
    auto& entities = _entities;
    for (int slot = 0; slot < MaxPlayers; ++slot)
        if (_movementServices[slot] == movementServices && entities.MovementServices(slot).Base() == movementServices)
            return slot;

    int found = -1;
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        _movementServices[slot] = entities.MovementServices(slot).Base();
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
    // Live clients keep the command number in the wrapper, not the protobuf payload.
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
    if (!PreCmd.Empty() || !FilterCmd.Empty())
        DecodeUserCmd(userCmd);
    // Filters run before observers see the command.
    FilterCmd.Raise(_preSlot, _cmdView);
    Pre.Raise(_preSlot);
    PreCmd.Raise(_preSlot, _cmdView);
    RETURN_META_VALUE(MRES_IGNORED, nullptr);
}

void* Movement::Hook_RunCommandPost(void* /*userCmd*/)
{
    // RunCommand is non-nested, so post reuses the slot resolved by pre.
    Post.Raise(_preSlot);
    RETURN_META_VALUE(MRES_IGNORED, nullptr);
}

}  // namespace VoltMod
