#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/GameData/BindingTypes.hpp>
#include <VoltMod/Engine/GameData/GameDataLookup.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace VoltMod
{

/** Every engine function, slot and offset the framework calls, typed and bound from gamedata. */
struct Bindings
{
    /** Take every member from @p lookup in one pass, clearing the last result. What did not bind
     *  is listed in @ref Failures. */
    Status Bind(const GameDataLookup& lookup);

    /** `key: reason` for each member the last @ref Bind left unbound. */
    std::vector<std::string> Failures;

    /** (className, forceEdictIndex). */
    Fn<CEntityInstance*(const char*, int)> CreateEntityByName;
    /** keyvalues may be null. */
    Fn<void(CEntityInstance*, CEntityKeyValues*)> DispatchSpawn;
    /** (entity, input, activator, caller, variant_t* value). */
    Fn<void(CEntityInstance*, const char*, CEntityInstance*, CEntityInstance*, void*)> AcceptInput;
    /** (entity system, target, input, activator, caller, value, delay, nullptr, nullptr). The queue
     *  copies the input and value. */
    Fn<void(void*, CEntityInstance*, const char*, CEntityInstance*, CEntityInstance*, const void*, float, const void*,
            const void*)>
        AddEntityIOEvent;
    Fn<void(CEntityInstance*, const char*)> SetModel;
    /** (entity, soundEvent, pitch, volume, delay). */
    Fn<void(CEntityInstance*, const char*, int, float, float)> EmitSoundParams;
    /** Returns its result through a hidden pointer, so Entity.cpp declares the prototype. */
    Address EmitSoundFilter;
    /** IGameEventListener2* (CPlayerSlot); GameEvents.cpp declares the prototype. */
    Address LegacyGameEventListener;
    /** CBaseEntity::TakeDamageOld(CTakeDamageInfo*, CTakeDamageResult*); see src/Hooks/DamageLayout.hpp. */
    Fn<int64_t(CEntityInstance*, void*, void*)> TakeDamage;
    /** CTakeDamageInfo(info, inflictor, attacker, ability, force, position, damage, type, custom, nullptr). */
    Fn<void(void*, CEntityInstance*, CEntityInstance*, CEntityInstance*, const Vector*, const Vector*, float, int, int,
            void*)>
        BuildDamageInfo;
    /** (CCSGameRules*, delay, CSRoundEndReason, int* team). */
    Fn<void(void*, float, uint32_t, void*)> TerminateRound;

    /** @defgroup CustomHudSetters CCSCustomHudLayout setters; they take `const CUtlString*`. @{ */
    Fn<void(void*, const CUtlString*, const CUtlString*, int32_t)> CustomHudSetHasClass;
    Fn<void(void*, int32_t, const CUtlString*, const CUtlString*, int32_t)> CustomHudSetHasClassForPlayer;
    Fn<void(void*, const CUtlString*, const CUtlString*, const CUtlString*)> CustomHudSetDialogVariable;
    Fn<void(void*, int32_t, const CUtlString*, const CUtlString*, const CUtlString*)>
        CustomHudSetDialogVariableForPlayer;
    Fn<void(void*, int32_t, bool)> CustomHudSetInputCapture;
    /** @} */

    /** Counted in CServerSideClient's message-filter base, which the hook receives. */
    VirtualFn<bool(INetworkMessageProcessingPreFilter*, const CNetMessage*, INetChannel*)> FilterMessage;
    /** CNetworkGameServer::ReplyConnection, which names the addons a connecting client mounts. */
    Fn<void(CNetworkGameServerBase*, EngineClient*)> ReplyConnection;

    /** IGameEventManager2** inside CSource2Server. */
    Address GameEventManager;

    /** The (bool explode, bool force) overload, counted in CCSPlayerPawn. */
    VirtualFn<void(CEntityInstance*, bool, bool)> CommitSuicide;
    VirtualFn<void(CEntityInstance*, int)> ChangeTeam;
    VirtualFn<void(CEntityInstance*)> Respawn;
    /** (origin, angles, velocity), each nullable; counted in CCSPlayerPawn. */
    VirtualFn<void(CEntityInstance*, const Vector*, const QAngle*, const Vector*)> Teleport;

    /** The overloads taking a filter, called on the stateless class table. */
    VirtualFn<bool(EngineNavPhysics*, const Vector*, const Vector*, CTraceFilter*, CGameTrace*)> NavTraceLine;
    VirtualFn<void(EngineNavPhysics*, const Ray_t*, const Vector*, const Vector*, CTraceFilter*, CGameTrace*)>
        NavTraceShape;
    /** (CUserCmd*), counted in CCSPlayer_MovementServices. */
    VirtualFn<void*(EngineMovementServices*, void*)> RunCommand;
    VirtualFn<void*(void*, const char*)> GiveNamedItem;
    /** (bool removeSuit). */
    VirtualFn<void(void*, bool)> RemoveAllItems;
    VirtualFn<bool(EngineClient*, const CNetMessage*)> ProcessRespondCvarValue;
    VirtualFn<bool(EngineClient*, const CNetMessage*, NetChannelBufType_t)> SendNetMessage;

    /** Inside IGameResourceService. */
    OffsetOf<CGameEntitySystem*> GameEntitySystem;
    /** The recipient's slot inside CCheckTransmitInfo. */
    OffsetOf<uint8_t> VisibilityRecipientSlot;
    OffsetOf<int> ClientSlot;
    /** Unaligned inside CServerSideClient. */
    OffsetOf<int64_t> ClientSteamId;
    /** From CServerSideClient to the base FilterMessage runs on. */
    OffsetOf<void> ClientMessageFilter;
    /** CNetworkGameServer::m_szAddons, a CUtlString. */
    OffsetOf<void> ServerAddons;
    /** The CSGOUserCmdPB inside CUserCmd. */
    OffsetOf<void> UserCmdProto;
    /** CUserCmd's own counter; live clients leave the protobuf one at zero. */
    OffsetOf<int32_t> UserCmdNumber;
};

}  // namespace VoltMod
