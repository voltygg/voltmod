#include <VoltMod/Core/Text/Strings.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace VoltMod
{

/** Binds one member from a gamedata lookup and keeps what did not bind. */
class MemberBinder
{
public:
    explicit MemberBinder(const GameDataLookup& lookup) : _lookup(lookup) {}

    template <class Sig>
    void operator()(Fn<Sig>& member, std::string_view key)
    {
        member = Fn<Sig>(Look(GameDataSection::Function, key).Address);
    }

    void operator()(Address& member, std::string_view key)
    {
        member = Address(Look(GameDataSection::Function | GameDataSection::Global, key).Address);
    }

    template <class Sig>
    void operator()(VirtualFn<Sig>& member, std::string_view key)
    {
        const GameDataLocation entry = Look(GameDataSection::VTable, key);
        member = VirtualFn<Sig>(entry.Value, entry.Address);
    }

    template <class T>
    void operator()(OffsetOf<T>& member, std::string_view key)
    {
        member = OffsetOf<T>(Look(GameDataSection::Offset, key).Value);
    }

    /** What did not bind, as `key: reason`, in the order the members were bound. */
    std::vector<std::string> TakeFailures() { return std::move(_failures); }

private:
    GameDataLocation Look(GameDataSection sections, std::string_view key)
    {
        const GameDataLocation entry = _lookup(sections, key);
        if (!entry.Found)
        {
            _failures.push_back(std::format("{}: {}", key, entry.Reason));
        }
        return entry;
    }

    const GameDataLookup& _lookup;
    std::vector<std::string> _failures;
};

Status Bindings::Bind(const GameDataLookup& lookup)
{
    *this = Bindings{};

    MemberBinder bind(lookup);

    bind(CreateEntityByName, "CreateEntityByName");
    bind(DispatchSpawn, "DispatchSpawn");
    bind(AcceptInput, "CEntityInstance::AcceptInput");
    bind(AddEntityIOEvent, "CEntitySystem::AddEntityIOEvent");
    bind(SetModel, "CBaseModelEntity::SetModel");
    bind(EmitSoundParams, "CBaseEntity::EmitSoundParams");
    bind(StopSound, "CBaseEntity::StopSound");
    bind(EmitSoundFilter, "CBaseEntity::EmitSoundFilter");
    bind(LegacyGameEventListener, "GetLegacyGameEventListener");
    bind(TakeDamage, "CBaseEntity::TakeDamageOld");
    bind(BuildDamageInfo, "CTakeDamageInfo::CTakeDamageInfo");
    bind(TerminateRound, "CCSGameRules::TerminateRound");

    bind(CustomHudSetHasClass, "CCSCustomHudLayout::SetHasClass");
    bind(CustomHudSetHasClassForPlayer, "CCSCustomHudLayout::SetHasClassForPlayer");
    bind(CustomHudSetDialogVariable, "CCSCustomHudLayout::SetDialogVariableString");
    bind(CustomHudSetDialogVariableForPlayer, "CCSCustomHudLayout::SetDialogVariableStringForPlayer");
    bind(CustomHudSetInputCapture, "CCSCustomHudLayout::SetInputCaptureEnabled");
    bind(FilterMessage, "INetworkMessageProcessingPreFilter::FilterMessage");
    bind(ReplyConnection, "CNetworkGameServer::ReplyConnection");

    bind(GameEventManager, "CSource2Server::g_GameEventManager");

    bind(CommitSuicide, "CBasePlayerPawn::CommitSuicide");
    bind(ChangeTeam, "CCSPlayerController::ChangeTeam");
    bind(Respawn, "CCSPlayerController::Respawn");
    bind(Teleport, "CBaseEntity::Teleport");
    bind(NavTraceLine, "CNavPhysicsInterface::Nav_TraceLine");
    bind(NavTraceShape, "CNavPhysicsInterface::Nav_TraceShape");
    bind(GiveNamedItem, "CCSPlayer_ItemServices::GiveNamedItem");
    bind(RemoveAllItems, "CCSPlayer_ItemServices::RemoveAllItems");
    bind(RunCommand, "CPlayer_MovementServices::RunCommand");
    bind(ProcessRespondCvarValue, "CServerSideClient::ProcessRespondCvarValue");
    bind(SendNetMessage, "CServerSideClient::SendNetMessage");

    bind(GameEntitySystem, "GameEntitySystem");
    bind(VisibilityRecipientSlot, "CheckTransmitPlayerSlot");
    bind(ClientSlot, "CServerSideClientBase::m_nClientSlot");
    bind(ClientMessageFilter, "CServerSideClient::INetworkMessageProcessingPreFilter");
    bind(ClientSteamId, "CServerSideClientBase::m_SteamID");
    bind(ServerAddons, "CNetworkGameServer::m_szAddons");
    bind(UserCmdProto, "CUserCmd::CSGOUserCmdPB");
    // Movement can use the protobuf counter instead.
    bind(UserCmdNumber, "CUserCmdBase::cmdNum");

    Failures = bind.TakeFailures();
    if (!Failures.empty())
    {
        return std::unexpected(
            Error::Engine(std::format("{} did not bind: {}", Failures.size(), Strings::Join(Failures, "; "))));
    }

    return {};
}

}  // namespace VoltMod
