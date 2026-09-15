#include <VoltMod/Core/EnumNames.hpp>
#include <VoltMod/Core/Strings.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/** Binds members, naming every key that does not bind. */
class Binder
{
public:
    Binder(const GameData& data, std::vector<std::string>& failures) : _data(data), _failures(failures) {}

    template <class Sig>
    void operator()(Fn<Sig>& member, std::string_view key)
    {
        if (const auto* entry = Claim(key, GameData::Kind::Signature))
            member = Fn<Sig>(entry->Address);
    }

    template <class Sig>
    void operator()(VirtualFn<Sig>& member, std::string_view key)
    {
        const auto* entry = Claim(key, GameData::Kind::VTable);
        if (!entry)
            return;

        // GameData::Address is the slot's code, read through any hook that replaced it.
        const bool holdsCode = entry->Table && entry->Address;
        member = VirtualFn<Sig>(entry->Index, holdsCode ? entry->Table : nullptr);
        if (!entry->Table)
            _failures.push_back(std::format("{}: no vtable for '{}' in '{}'", key, entry->Class, entry->Library));
        else if (!entry->Address)
            _failures.push_back(std::format("{}: {}::[{}] does not hold code", key, entry->Class, entry->Index));
    }

    template <class T>
    void operator()(OffsetOf<T>& member, std::string_view key)
    {
        if (const auto* entry = Claim(key, GameData::Kind::Offset))
            member = OffsetOf<T>(entry->Index);
    }

    void Signature(Address& member, std::string_view key)
    {
        if (const auto* entry = Claim(key, GameData::Kind::Signature))
            member = Address(entry->Address);
    }

    void Global(Address& member, std::string_view key)
    {
        if (const auto* entry = Claim(key, GameData::Kind::Address))
            member = Address(entry->Address);
    }

private:
    const GameData::Resolution* Claim(std::string_view key, GameData::Kind section)
    {
        const auto it = _data.Resolutions().find(std::string(key));
        if (it == _data.Resolutions().end())
            _failures.push_back(std::format("'{}' is not in gamedata", key));
        else if (it->second.Section != section)
            _failures.push_back(
                std::format("'{}' is a {} entry, not a {} one", key, Name(it->second.Section), Name(section)));
        else if (!it->second.Error.empty())
            _failures.push_back(std::format("{}: {}", key, it->second.Error));
        else
            return &it->second;

        return nullptr;
    }

    const GameData& _data;
    std::vector<std::string>& _failures;
};

Status Bindings::Bind(const GameData& data)
{
    Failures.clear();
    if (data.Resolutions().empty())
        return std::unexpected(Error::NotReady("gamedata is empty; nothing to bind"));

    Binder bind(data, Failures);

    bind(CreateEntityByName, "CreateEntityByName");
    bind(DispatchSpawn, "DispatchSpawn");
    bind(AcceptInput, "CEntityInstance::AcceptInput");
    bind(AddEntityIOEvent, "CEntitySystem::AddEntityIOEvent");
    bind(UtilRemove, "UTIL_Remove");
    bind(SetModel, "CBaseModelEntity::SetModel");
    bind(EmitSoundParams, "CBaseEntity::EmitSoundParams");
    bind.Signature(EmitSoundFilter, "CBaseEntity::EmitSoundFilter");
    bind(FindEntityByClassName, "CGameEntitySystem::FindEntityByClassName");
    bind(FindEntityByName, "CGameEntitySystem::FindEntityByName");
    bind.Signature(LegacyGameEventListener, "GetLegacyGameEventListener");

    bind(CustomHudSetHasClass, "CCSCustomHudLayout::SetHasClass");
    bind(CustomHudSetHasClassForPlayer, "CCSCustomHudLayout::SetHasClassForPlayer");
    bind(CustomHudSetDialogVariable, "CCSCustomHudLayout::SetDialogVariableString");
    bind(CustomHudSetDialogVariableForPlayer, "CCSCustomHudLayout::SetDialogVariableStringForPlayer");
    bind(CustomHudSetInputCapture, "CCSCustomHudLayout::SetInputCaptureEnabled");
    bind(FilterMessage, "INetworkMessageProcessingPreFilter::FilterMessage");
    bind(ReplyConnection, "CNetworkGameServer::ReplyConnection");

    bind.Global(GameEventManager, "CSource2Server::g_GameEventManager");
    bind.Global(GameSystemFactoryList, "CBaseGameSystemFactory::sm_pFirst");
    bind.Global(GameSystemEventDispatcher, "IGameSystem::pEventDispatcher");
    bind.Global(GameSystemList, "IGameSystem::s_GameSystems");

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

    if (Failures.empty())
        return {};
    return std::unexpected(
        Error::Engine(std::format("{} did not bind: {}", Failures.size(), Strings::Join(Failures, "; "))));
}

}  // namespace VoltMod
