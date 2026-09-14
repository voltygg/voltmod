#include <VoltMod/Core/EnumNames.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <array>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace VoltMod
{

/** Take the vtable GameData located at load, once it reports the slot holds code. */
static Result<ClassVTable> BindClassVTable(const GameData::Resolution& entry)
{
    if (!entry.Table)
        return std::unexpected(Error::Engine(std::format("no vtable for '{}' in '{}'", entry.Class, entry.Library)));

    // GameData::Address is the slot's code, read through any hook that replaced it.
    if (!entry.Address)
        return std::unexpected(Error::Engine(std::format("{}::[{}] does not hold code", entry.Class, entry.Index)));

    return ClassVTable(entry.Class, entry.Table);
}

/** Binds members and records the first failure for each capability. */
class Binder
{
public:
    Binder(const GameData& data, Capabilities& caps) : _data(data), _caps(caps) {}

    template <class Sig>
    void operator()(Fn<Sig>& member, std::string_view key, std::optional<Capability> capability = {})
    {
        if (const auto* entry = Claim(key, GameData::Kind::Signature, capability))
            member = Fn<Sig>(entry->Address);
    }

    template <class Sig>
    void operator()(VirtualFn<Sig>& member, std::string_view key, std::optional<Capability> capability = {})
    {
        if (const auto* entry = Claim(key, GameData::Kind::VTable, capability))
            member = VirtualFn<Sig>(entry->Index);
    }

    template <class T>
    void operator()(OffsetOf<T>& member, std::string_view key, std::optional<Capability> capability = {})
    {
        if (const auto* entry = Claim(key, GameData::Kind::Offset, capability))
            member = OffsetOf<T>(entry->Index);
    }

    void Signature(Address& member, std::string_view key, std::optional<Capability> capability = {})
    {
        if (const auto* entry = Claim(key, GameData::Kind::Signature, capability))
            member = Address(entry->Address);
    }

    void Global(Address& member, std::string_view key, std::optional<Capability> capability = {})
    {
        if (const auto* entry = Claim(key, GameData::Kind::Address, capability))
            member = Address(entry->Address);
    }

    template <class Object, class Sig>
    void operator()(ClassSlot<Object, Sig>& member, std::string_view key, Capability capability)
    {
        const auto* entry = Claim(key, GameData::Kind::VTable, capability);
        if (!entry)
            return;

        member.Function = VirtualFn<Sig>(entry->Index);
        if (auto table = BindClassVTable(*entry))
            member.Table = std::move(*table);
        else
            Fail(capability, std::move(table.error().Detail));
    }

    void Finish()
    {
        for (Capability capability : EnumValues<Capability>())
        {
            const auto index = EnumIndex(capability);
            auto& result = _results[index];
            if (!result.Seen)
                continue;

            const bool ok = result.Error.empty();
            _caps.Set(capability, ok, std::move(result.Error));
        }
    }

private:
    struct CapabilityResult
    {
        bool Seen = false;
        std::string Error;
    };

    const GameData::Resolution* Claim(std::string_view key, GameData::Kind section,
                                      std::optional<Capability> capability)
    {
        if (capability)
            _results[EnumIndex(*capability)].Seen = true;

        const auto it = _data.Resolutions().find(std::string(key));
        std::string error;
        if (it == _data.Resolutions().end())
            error = std::format("'{}' is not in gamedata", key);
        else if (it->second.Section != section)
            error = std::format("'{}' is a {} entry, not a {} one", key, Name(it->second.Section), Name(section));
        else if (!it->second.Error.empty())
            error = std::format("{}: {}", key, it->second.Error);
        else
            return &it->second;

        if (capability)
            Fail(*capability, std::move(error));
        else
            Log::Warn("Bindings: {}", error);

        return nullptr;
    }

    void Fail(Capability capability, std::string reason)
    {
        auto& result = _results[EnumIndex(capability)];
        if (result.Error.empty())
            result.Error = std::move(reason);
    }

    const GameData& _data;
    Capabilities& _caps;
    std::array<CapabilityResult, EnumCount<Capability>> _results{};
};

Status Bindings::Bind(const GameData& data, Capabilities& caps)
{
    if (data.Resolutions().empty())
        return std::unexpected(Error::NotReady("gamedata is empty; nothing to bind"));

    Binder bind(data, caps);

    bind(CreateEntityByName, "CreateEntityByName", Capability::EntityOps);
    bind(DispatchSpawn, "DispatchSpawn", Capability::EntityOps);
    bind(AcceptInput, "CEntityInstance::AcceptInput");
    bind(AddEntityIOEvent, "CEntitySystem::AddEntityIOEvent");
    bind(UtilRemove, "UTIL_Remove");
    bind(SetModel, "CBaseModelEntity::SetModel");
    bind(EmitSoundParams, "CBaseEntity::EmitSoundParams");
    bind.Signature(EmitSoundFilter, "CBaseEntity::EmitSoundFilter");
    bind(FindEntityByClassName, "CGameEntitySystem::FindEntityByClassName");
    bind(FindEntityByName, "CGameEntitySystem::FindEntityByName");
    bind.Signature(LegacyGameEventListener, "GetLegacyGameEventListener");

    // These five share one capability, so a partial match disables all custom HUD writes.
    bind(CustomHudSetHasClass, "CCSCustomHudLayout::SetHasClass", Capability::CustomUi);
    bind(CustomHudSetHasClassForPlayer, "CCSCustomHudLayout::SetHasClassForPlayer", Capability::CustomUi);
    bind(CustomHudSetDialogVariable, "CCSCustomHudLayout::SetDialogVariableString", Capability::CustomUi);
    bind(CustomHudSetDialogVariableForPlayer, "CCSCustomHudLayout::SetDialogVariableStringForPlayer", Capability::CustomUi);
    bind(CustomHudSetInputCapture, "CCSCustomHudLayout::SetInputCaptureEnabled", Capability::CustomUi);
    bind(FilterMessage, "INetworkMessageProcessingPreFilter::FilterMessage", Capability::UiClicks);
    bind(ReplyConnection, "CNetworkGameServer::ReplyConnection", Capability::Addons);

    bind.Global(GameEventManager, "CSource2Server::g_GameEventManager", Capability::GameEvents);
    bind.Global(GameSystemFactoryList, "CBaseGameSystemFactory::sm_pFirst", Capability::Precache);
    bind.Global(GameSystemEventDispatcher, "IGameSystem::pEventDispatcher", Capability::Precache);
    bind.Global(GameSystemList, "IGameSystem::s_GameSystems", Capability::Precache);

    bind(CommitSuicide, "CBasePlayerPawn::CommitSuicide");
    bind(ChangeTeam, "CCSPlayerController::ChangeTeam");
    bind(Respawn, "CCSPlayerController::Respawn");
    bind(Teleport, "CBaseEntity::Teleport", Capability::Teleport);
    bind(GiveNamedItem, "CCSPlayer_ItemServices::GiveNamedItem", Capability::Items);
    bind(RemoveAllItems, "CCSPlayer_ItemServices::RemoveAllItems", Capability::Items);
    bind(RunCommand, "CPlayer_MovementServices::RunCommand", Capability::Movement);
    bind(ProcessRespondCvarValue, "CServerSideClient::ProcessRespondCvarValue", Capability::ClientConVars);
    bind(SendNetMessage, "CServerSideClient::SendNetMessage", Capability::Addons);

    bind(GameEntitySystem, "GameEntitySystem", Capability::Entities);
    bind(VisibilityRecipientSlot, "CheckTransmitPlayerSlot", Capability::Visibility);
    // Shared offsets bind once per capability so each disabled feature records its reason.
    bind(ClientSlot, "CServerSideClientBase::m_nClientSlot", Capability::ClientConVars);
    bind(ClientSlot, "CServerSideClientBase::m_nClientSlot", Capability::UiClicks);
    bind(ClientSlot, "CServerSideClientBase::m_nClientSlot", Capability::Addons);
    bind(ClientMessageFilter, "CServerSideClient::INetworkMessageProcessingPreFilter", Capability::UiClicks);
    bind(ServerClients, "CNetworkGameServer::m_Clients", Capability::Addons);
    bind(ClientSteamId, "CServerSideClientBase::m_SteamID", Capability::Addons);
    bind(ServerAddons, "CNetworkGameServer::m_szAddons", Capability::Addons);
    bind(UserCmdProto, "CUserCmd::CSGOUserCmdPB", Capability::Movement);
    // Optional: movement can use the protobuf counter instead.
    bind(UserCmdNumber, "CUserCmdBase::cmdNum");

    bind.Finish();
    return {};
}

}  // namespace VoltMod
