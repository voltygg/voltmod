#include "Sdk/Internal/VtableLookup.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/MetamodGlobals.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Detail/Runtime.hpp>
#include <VoltMod/Runtime.hpp>
#include <VoltMod/Sdk/Engine/GameData.hpp>
#include <VoltMod/Sdk/Entity/Entity.hpp>
#include <VoltMod/Sdk/Movement/MovementHook.hpp>
#include <algorithm>
#include <cs_usercmd.pb.h>
#include <cstring>

PLUGIN_GLOBALVARS();

namespace VoltMod::Sdk
{
using namespace VoltMod::Core;

// void* return/param stand in for the real CPlayer_MovementServices::RunCommand(CUserCmd*)
// signature - a pre/post observer never touches either. The vtable index is reconfigured
// from gamedata at install time. Bound to the class vtable (DVP hook), so it fires for every
// player without needing a live instance to bind to.
SH_DECL_MANUALHOOK1(VoltMod_MovementRunCommand, 0, 0, 0, void*, void*);

namespace
{

// The server module owning the concrete movement-services class every player pawn instantiates.
constexpr const char* ServerModule = "server";
constexpr const char* MovementServicesClass = "CCSPlayer_MovementServices";

// Far above the real value (8), only to catch drifted or hand-edited gamedata before it turns into
// a read past the CUserCmd object.
constexpr int MaxUserCmdOffset = 4096;

}  // namespace

bool MovementHook::Install()
{
    if (_installed)
        return true;

    int index = VoltMod::Detail::Rt().GameData.GetVtableIndex("RunCommand");
    if (index < 0)
        return false;

    void* vtable = FindVirtualTable(ServerModule, MovementServicesClass);
    if (!vtable)
        return false;  // FindVirtualTable already logged which step failed

    // The class name drifts like the index does, and nothing else here would notice: a wrong name
    // resolves to some other class's table and the hook then never fires. Whenever a pawn happens to
    // be live, its own vptr is the ground truth to check against - but Install() must still work
    // from OnLoad, with no player connected, so a mismatch only warns.
    for (int slot = 0; slot < Core::MaxPlayers; ++slot)
    {
        void* instance = VoltMod::Detail::Rt().Entities.GetPlayerMovementServices(slot);
        if (!instance)
            continue;
        if (*static_cast<void**>(instance) != vtable)
            Log::Warn("MovementHook: a live pawn's movement services vtable differs from {}; wrong class name?",
                      MovementServicesClass);
        break;
    }

    _pbOffset = VoltMod::Detail::Rt().GameData.GetByteOffset("UserCmdPB", MaxUserCmdOffset, alignof(void*));
    if (_pbOffset < 0)
        Log::Warn("MovementHook: no usable 'UserCmdPB' offset; cmd listeners get Valid=false views.");

    _cmdNumberOffset =
        VoltMod::Detail::Rt().GameData.GetByteOffset("UserCmdNumber", MaxUserCmdOffset, alignof(int32_t));
    if (_cmdNumberOffset < 0)
        Log::Warn(
            "MovementHook: no usable 'UserCmdNumber' offset; falling back to the protobuf's "
            "legacy_command_number, which the live client leaves at 0.");

    _movementServices.fill(nullptr);

    SH_MANUALHOOK_RECONFIGURE(VoltMod_MovementRunCommand, index, 0, 0);
    _preHookId = SH_ADD_MANUALDVPHOOK(VoltMod_MovementRunCommand, vtable,
                                      SH_MEMBER(this, &MovementHook::Hook_RunCommandPre), false);
    _postHookId = SH_ADD_MANUALDVPHOOK(VoltMod_MovementRunCommand, vtable,
                                       SH_MEMBER(this, &MovementHook::Hook_RunCommandPost), true);
    if (_preHookId == 0 || _postHookId == 0)
    {
        // Half a hook is worse than none: post would reuse a slot pre never resolved.
        if (_preHookId != 0)
            SH_REMOVE_HOOK_ID(_preHookId);
        if (_postHookId != 0)
            SH_REMOVE_HOOK_ID(_postHookId);
        _preHookId = 0;
        _postHookId = 0;
        Log::Warn("MovementHook: SourceHook refused the RunCommand hook; movement listeners disabled.");
        return false;
    }

    _installed = true;
    Log::Info("Movement RunCommand hook installed on {} vtable (index {}).", MovementServicesClass, index);
    return true;
}

void MovementHook::Remove()
{
    if (!_installed)
        return;

    // Removal by id never dereferences the hooked instance, so this is safe even after a map change
    // has destroyed every pawn.
    SH_REMOVE_HOOK_ID(_preHookId);
    SH_REMOVE_HOOK_ID(_postHookId);

    _installed = false;
    _preHookId = 0;
    _postHookId = 0;
    // Every cached pointer belongs to a pawn that may be freed before the next Install().
    _movementServices.fill(nullptr);
}

void MovementHook::RemoveListener(uint64_t id)
{
    _pre.Remove(id);
    _post.Remove(id);
    _preCmd.Remove(id);
    _postCmd.Remove(id);
    _filter.Remove(id);
}

int MovementHook::SlotFromMovementServices(void* movementServices) const
{
    if (!movementServices)
        return -1;

    // Cache this hot lookup, but confirm each hit so a recycled pointer cannot map
    // to the wrong slot. A stale hit falls through to the rescan.
    auto& entities = VoltMod::Detail::Rt().Entities;
    for (int slot = 0; slot < Core::MaxPlayers; ++slot)
        if (_movementServices[slot] == movementServices && entities.GetPlayerMovementServices(slot) == movementServices)
            return slot;

    int found = -1;
    for (int slot = 0; slot < Core::MaxPlayers; ++slot)
    {
        _movementServices[slot] = entities.GetPlayerMovementServices(slot);
        if (_movementServices[slot] == movementServices)
            found = slot;
    }
    return found;
}

void MovementHook::DecodeUserCmd(void* userCmd)
{
    _cmdView = {};
    if (!userCmd || _pbOffset < 0)
        return;

    const auto* pb = reinterpret_cast<const CSGOUserCmdPB*>(static_cast<char*>(userCmd) + _pbOffset);
    const auto& base = pb->base();

    _cmdView.Valid = true;
    _cmdView.ClientTick = base.client_tick();
    // The counter the engine maintains lives in the CUserCmd wrapper, not the payload:
    // legacy_command_number stays 0 on a live client, so it is only a fallback.
    if (_cmdNumberOffset >= 0)
        std::memcpy(&_cmdView.CommandNumber, static_cast<const char*>(userCmd) + _cmdNumberOffset,
                    sizeof(_cmdView.CommandNumber));
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

void* MovementHook::Hook_RunCommandPre(void* userCmd)
{
    _preSlot = SlotFromMovementServices(META_IFACEPTR(void));
    if (!_preCmd.Empty() || !_postCmd.Empty() || !_filter.Empty())
        DecodeUserCmd(userCmd);
    // Filters edit the decoded view before anyone reads it, so pre/preCmd/postCmd listeners
    // and InputHistory all observe the same edited command.
    _filter.Dispatch([&](auto& filter) { filter(_preSlot, _cmdView); });
    _pre.Dispatch([&](auto& callback) { callback(_preSlot); });
    _preCmd.Dispatch([&](auto& callback) { callback(_preSlot, _cmdView); });
    RETURN_META_VALUE(MRES_IGNORED, nullptr);
}

void* MovementHook::Hook_RunCommandPost(void* /*userCmd*/)
{
    // Post always brackets the same RunCommand call as the preceding pre (movement is
    // processed one player at a time, no nesting), so reuse the pre-resolved slot and
    // the pre-decoded cmd view rather than repeating the work.
    _post.Dispatch([&](auto& callback) { callback(_preSlot); });
    _postCmd.Dispatch([&](auto& callback) { callback(_preSlot, _cmdView); });
    RETURN_META_VALUE(MRES_IGNORED, nullptr);
}

}  // namespace VoltMod::Sdk
