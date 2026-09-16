#include "Engine/GameData/GameDataDocument.hpp"
#include "Engine/GameData/GameDataResolver.hpp"
#include "Engine/GameData/ResolvedRecord.hpp"

#include <VoltMod/Core/Json.hpp>
#include <VoltMod/Core/Strings.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <format>
#include <string_view>

namespace VoltMod
{

/** Assigns each typed member from the resolver, so the binding list reads as member and key. */
class MemberBinder
{
public:
    explicit MemberBinder(GameDataResolver& resolver) : _resolver(resolver) {}

    template <class Sig>
    void operator()(Fn<Sig>& member, std::string_view key)
    {
        member = Fn<Sig>(_resolver.Function(key));
    }

    void operator()(Address& member, std::string_view key) { member = Address(_resolver.FunctionOrGlobal(key)); }

    template <class Sig>
    void operator()(VirtualFn<Sig>& member, std::string_view key)
    {
        const VirtualSlot slot = _resolver.Slot(key);
        member = VirtualFn<Sig>(slot.Index, slot.Table);
    }

    template <class T>
    void operator()(OffsetOf<T>& member, std::string_view key)
    {
        member = OffsetOf<T>(_resolver.Offset(key));
    }

private:
    GameDataResolver& _resolver;
};

Status Bindings::Load(std::string_view path, const OriginalSlotLookup& originalOf)
{
    *this = Bindings{};

    const auto file = Json::ReadFile<GameDataDocument, Json::StrictReadOptions>(path);
    if (!file)
        return std::unexpected(file.error());

    GameDataResolver resolver(*file, originalOf);
    MemberBinder bind(resolver);

    bind(CreateEntityByName, "CreateEntityByName");
    bind(DispatchSpawn, "DispatchSpawn");
    bind(AcceptInput, "CEntityInstance::AcceptInput");
    bind(AddEntityIOEvent, "CEntitySystem::AddEntityIOEvent");
    bind(UtilRemove, "UTIL_Remove");
    bind(SetModel, "CBaseModelEntity::SetModel");
    bind(EmitSoundParams, "CBaseEntity::EmitSoundParams");
    bind(EmitSoundFilter, "CBaseEntity::EmitSoundFilter");
    bind(FindEntityByClassName, "CGameEntitySystem::FindEntityByClassName");
    bind(FindEntityByName, "CGameEntitySystem::FindEntityByName");
    bind(LegacyGameEventListener, "GetLegacyGameEventListener");

    bind(CustomHudSetHasClass, "CCSCustomHudLayout::SetHasClass");
    bind(CustomHudSetHasClassForPlayer, "CCSCustomHudLayout::SetHasClassForPlayer");
    bind(CustomHudSetDialogVariable, "CCSCustomHudLayout::SetDialogVariableString");
    bind(CustomHudSetDialogVariableForPlayer, "CCSCustomHudLayout::SetDialogVariableStringForPlayer");
    bind(CustomHudSetInputCapture, "CCSCustomHudLayout::SetInputCaptureEnabled");
    bind(FilterMessage, "INetworkMessageProcessingPreFilter::FilterMessage");
    bind(ReplyConnection, "CNetworkGameServer::ReplyConnection");

    bind(GameEventManager, "CSource2Server::g_GameEventManager");
    bind(GameSystemFactoryList, "CBaseGameSystemFactory::sm_pFirst");
    bind(GameSystemEventDispatcher, "IGameSystem::pEventDispatcher");
    bind(GameSystemList, "IGameSystem::s_GameSystems");

    bind(CommitSuicide, "CBasePlayerPawn::CommitSuicide");
    bind(ChangeTeam, "CCSPlayerController::ChangeTeam");
    bind(Respawn, "CCSPlayerController::Respawn");
    bind(Teleport, "CBaseEntity::Teleport");
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
    // Optional: movement can use the protobuf counter instead.
    bind(UserCmdNumber, "CUserCmdBase::cmdNum");

    resolver.LogSummary(path);
    Failures = resolver.Failures();
    if (!Failures.empty())
        return std::unexpected(
            Error::Engine(std::format("{} did not bind: {}", Failures.size(), Strings::Join(Failures, "; "))));

    WriteResolvedRecord(resolver.Record());
    return {};
}

}  // namespace VoltMod
