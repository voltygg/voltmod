#include "Engine/SigScanner.hpp"
#include "Engine/VtableLookup.hpp"

#include <VoltMod/Core/EnumNames.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <array>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace VoltMod
{

/** Resolve a class vtable and verify that the configured slot contains code. */
static Result<VTableRef> BindVTable(const GameData::Resolution& entry)
{
    void* table = FindVirtualTable(entry.Library.c_str(), entry.Class.c_str());
    if (!table)
        return std::unexpected(Error::Engine(std::format("no vtable for '{}' in '{}'", entry.Class, entry.Library)));

    // Accept executable trampolines installed by other hooks.
    if (!IsExecutableAddress(static_cast<void**>(table)[entry.Index]))
        return std::unexpected(Error::Engine(std::format("{}::[{}] does not hold code", entry.Class, entry.Index)));

    return VTableRef(entry.Class, table);
}

/** Binds each member and records the first failure for each capability. */
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
    void operator()(VFn<Sig>& member, std::string_view key, std::optional<Capability> capability = {})
    {
        if (const auto* entry = Claim(key, GameData::Kind::VTable, capability))
            member = VFn<Sig>(entry->Index);
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

    template <class Sig>
    void operator()(VHookBinding<Sig>& member, std::string_view key, Capability capability)
    {
        const auto* entry = Claim(key, GameData::Kind::VTable, capability);
        if (!entry)
            return;

        member.Method = VFn<Sig>(entry->Index);
        if (auto table = BindVTable(*entry))
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

    // Signatures
    bind(CreateEntityByName, "CreateEntityByName", Capability::EntityOps);
    bind(DispatchSpawn, "DispatchSpawn", Capability::EntityOps);
    bind(AcceptInput, "CEntityInstance_AcceptInput");
    bind(AddEntityIOEvent, "CEntitySystem_AddEntityIOEvent");
    bind(UtilRemove, "UTIL_Remove");
    bind(SetModel, "CBaseModelEntity_SetModel");
    bind(EmitSoundParams, "CBaseEntity_EmitSoundParams");
    bind.Signature(EmitSoundFilter, "CBaseEntity_EmitSoundFilter");
    bind(FindEntityByClassName, "CGameEntitySystem_FindEntityByClassName");
    bind(FindEntityByName, "CGameEntitySystem_FindEntityByName");
    bind.Signature(LegacyGameEventListener, "LegacyGameEventListener");

    // Addresses
    bind.Global(GameEventManager, "GameEventManager", Capability::GameEvents);
    bind.Global(GameSystemFactoryList, "GameSystemFactoryList", Capability::Precache);
    bind.Global(GameSystemEventDispatcher, "GameSystemEventDispatcher", Capability::Precache);
    bind.Global(GameSystemList, "GameSystemList", Capability::Precache);

    // Virtual functions
    bind(CommitSuicide, "CommitSuicide");
    bind(ChangeTeam, "ChangeTeam");
    bind(Respawn, "Respawn");
    bind(Teleport, "Teleport", Capability::Teleport);
    bind(GiveNamedItem, "GiveNamedItem", Capability::Items);
    bind(RemoveAllItems, "RemoveAllItems", Capability::Items);
    bind(RunCommand, "RunCommand", Capability::Movement);
    bind(ProcessRespondCvarValue, "ProcessRespondCvarValue", Capability::ClientCvars);

    // Offsets
    bind(GameEntitySystem, "GameEntitySystem", Capability::Entities);
    bind(CheckTransmitPlayerSlot, "CheckTransmitPlayerSlot", Capability::Transmit);
    bind(ServerSideClientSlot, "ServerSideClientSlot", Capability::ClientCvars);
    bind(UserCmdPB, "UserCmdPB", Capability::Movement);
    // Optional: movement can use the protobuf counter instead.
    bind(UserCmdNumber, "UserCmdNumber");

    bind.Finish();
    return {};
}

}  // namespace VoltMod
