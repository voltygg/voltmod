#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/MetamodGlobals.hpp>
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

Movement::Movement(EntitySystem& entities, const Bindings& bindings, Capabilities& capabilities)
    : _lifecycle(
          "Movement", [this] { return StartHook(); }, [this] { StopHook(); }),
      Rewrite(_lifecycle.ForEvent()),
      Before(_lifecycle.ForEvent()),
      After(_lifecycle.ForEvent()),
      _entities(entities),
      _capabilities(capabilities),
      _bindings(bindings)
{}

// A subscription that outlives this object leaves a hook into an unloaded module; _lifecycle logs it.
Movement::~Movement() = default;

bool Movement::StartHook()
{
    if (!_bindings.UserCmdPB)
        Log::Warn("Movement: no usable 'UserCmdPB' offset; handlers get Valid=false commands.");

    if (!_bindings.UserCmdNumber)
        Log::Warn(
            "Movement: no usable 'UserCmdNumber' offset; falling back to the protobuf's "
            "legacy_command_number, which the live client leaves at 0.");

    auto hook = VtableHook::OnVTable<VoltMod_MovementRunCommandHook>(
        "Movement RunCommand", _bindings.RunCommand, this, &Movement::Hook_RunCommandPre,
        &Movement::Hook_RunCommandPost, LiveMovementServices());
    if (!hook)
    {
        // Bindings marked the capability usable from gamedata alone; a failed install retracts it.
        Log::Warn("Movement: {}; movement handlers will not fire.", hook.error().Detail);
        _capabilities.Set(Capability::Movement, false, hook.error().Detail);
        return false;
    }

    _hook = std::move(*hook);
    _capabilities.Set(Capability::Movement, true);
    return true;
}

void Movement::StopHook()
{
    _hook.Reset();
}

void* Movement::LiveMovementServices()
{
    for (int slot = 0; slot < MaxPlayers; ++slot)
        if (Schema::CPlayer_MovementServices instance = _entities.MovementServices(slot))
            return instance.Base();
    return nullptr;
}

int Movement::SlotOf(void* movementServices)
{
    // The component's owner is the pawn, and the pawn knows its controller: no roster scan.
    CEntityInstance* pawn = Schema::CPlayer_MovementServices{movementServices}.OwnerEntity();
    return pawn ? Pawn{_entities, pawn}.Slot() : -1;
}

void Movement::Decode(const void* userCmd)
{
    _cmd = {};
    if (!userCmd || !_bindings.UserCmdPB)
        return;

    const auto* pb = static_cast<const CSGOUserCmdPB*>(_bindings.UserCmdPB.Ptr(userCmd));
    const auto& base = pb->base();

    _cmd.Valid = true;
    _cmd.ClientTick = base.client_tick();
    // Live clients keep the command number in the wrapper, not the protobuf payload.
    _cmd.CommandNumber = _bindings.UserCmdNumber ? _bindings.UserCmdNumber.Read(userCmd) : base.legacy_command_number();
    _cmd.HasViewAngles = base.has_viewangles();
    if (_cmd.HasViewAngles)
    {
        _cmd.ViewPitch = base.viewangles().x();
        _cmd.ViewYaw = base.viewangles().y();
        _cmd.ViewRoll = base.viewangles().z();
    }
    _cmd.ForwardMove = base.forwardmove();
    _cmd.LeftMove = base.leftmove();
    if (base.has_buttons_pb())
    {
        _cmd.ButtonsHeld = base.buttons_pb().buttonstate1();
        _cmd.ButtonsChanged = base.buttons_pb().buttonstate2();
    }
    _cmd.MouseDx = base.mousedx();
    _cmd.MouseDy = base.mousedy();
    _cmd.Attack1StartHistoryIndex = pb->attack1_start_history_index();
    _cmd.Attack2StartHistoryIndex = pb->attack2_start_history_index();

    _cmd.SubtickMoveCount = std::min(base.subtick_moves_size(), PlayerInput::MaxSubtickMoves);
    for (int i = 0; i < _cmd.SubtickMoveCount; ++i)
    {
        const auto& move = base.subtick_moves(i);
        _cmd.SubtickMoves[i] = {
            .Button = move.button(),
            .Pressed = move.pressed(),
            .When = move.when(),
            .PitchDelta = move.pitch_delta(),
            .YawDelta = move.yaw_delta(),
        };
    }

    _cmd.InputHistoryTotalCount = pb->input_history_size();
    _cmd.InputHistorySampleCount = std::min(_cmd.InputHistoryTotalCount, PlayerInput::MaxInputHistory);
    for (int i = 0; i < _cmd.InputHistorySampleCount; ++i)
    {
        const auto& entry = pb->input_history(i);
        auto& sample = _cmd.InputHistorySamples[i];
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
    _slot = SlotOf(META_IFACEPTR(void));
    Decode(userCmd);
    Rewrite.Raise(_slot, _cmd);
    Before.Raise(_slot, _cmd);
    RETURN_META_VALUE(MRES_IGNORED, nullptr);
}

void* Movement::Hook_RunCommandPost(void* /*userCmd*/)
{
    After.Raise(_slot, _cmd);
    RETURN_META_VALUE(MRES_IGNORED, nullptr);
}

}  // namespace VoltMod
