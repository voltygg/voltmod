#include "Engine/SigScanner.hpp"
#include "Engine/VtableLookup.hpp"

#include <VoltMod/Core/EnumNames.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <format>
#include <initializer_list>
#include <string>
#include <string_view>

namespace VoltMod
{

/** One gamedata key a capability needs, and the section it must have come from. */
struct RequiredEntry
{
    std::string_view Key;
    GameData::Kind Section;
};

/** The resolution for @p key, or nullptr when gamedata has no such key. */
static const GameData::Resolution* Find(const GameData& data, std::string_view key)
{
    auto it = data.Resolutions().find(std::string(key));
    return it == data.Resolutions().end() ? nullptr : &it->second;
}

/** Why @p key is unusable as a @p section entry, or empty when it resolved. */
static std::string EntryError(const GameData& data, std::string_view key, GameData::Kind section)
{
    const auto* entry = Find(data, key);
    if (!entry)
        return std::format("'{}' is not in gamedata", key);
    if (entry->Section != section)
        return std::format("'{}' is a {} entry, not a {} one", key, Name(entry->Section), Name(section));
    if (!entry->Error.empty())
        return std::format("{}: {}", key, entry->Error);
    return {};
}

/** The resolved address of a signature or address entry, or nullptr. */
static void* AddressOf(const GameData& data, std::string_view key)
{
    const auto* entry = Find(data, key);
    return entry && entry->Error.empty() ? entry->Address : nullptr;
}

/** The resolved index of a vtable or offset entry, or -1. */
static int IndexOf(const GameData& data, std::string_view key)
{
    const auto* entry = Find(data, key);
    return entry && entry->Error.empty() ? entry->Index : -1;
}

/**
 * Locate the class vtable a DVP hook binds to, and confirm the slot @p key names holds code.
 *
 * The class name drifts exactly like the index does, and a name that resolves to some other
 * class's table would leave the hook silently never firing. Checking that the slot holds code
 * catches both that and an index past the end of a real table.
 */
static Result<VTableRef> BindVTable(const GameData& data, std::string_view key)
{
    const auto* entry = Find(data, key);
    if (!entry || entry->Section != GameData::Kind::VTable || !entry->Error.empty())
        return std::unexpected(Error::Unsupported(EntryError(data, key, GameData::Kind::VTable)));

    void* table = FindVirtualTable(entry->Library.c_str(), entry->Class.c_str());
    if (!table)  // FindVirtualTable already logged which step failed
        return std::unexpected(Error::Engine(std::format("no vtable for '{}' in '{}'", entry->Class, entry->Library)));

    // A slot another plugin has already hooked points at that hook's trampoline, which is still
    // code; only a slot holding data means the table or the index is wrong.
    if (!IsExecutableAddress(static_cast<void**>(table)[entry->Index]))
        return std::unexpected(Error::Engine(std::format("{}::[{}] does not hold code", entry->Class, entry->Index)));

    return VTableRef(entry->Class, table);
}

/** Record @p capability from the keys it needs; the first failure is the reason. */
static void Gate(const GameData& data, Capabilities& caps, Capability capability,
                 std::initializer_list<RequiredEntry> required)
{
    for (const auto& entry : required)
    {
        std::string error = EntryError(data, entry.Key, entry.Section);
        if (!error.empty())
        {
            caps.Set(capability, false, std::move(error));
            return;
        }
    }
    caps.Set(capability, true);
}

/** Bind one DVP-hook table, folding a failure into the capability that needs it. */
static void GateTable(const GameData& data, Capabilities& caps, Capability capability, std::string_view key,
                      VTableRef& target)
{
    if (!caps.Has(capability))
        return;  // an earlier key already failed this capability; keep the first reason

    auto table = BindVTable(data, key);
    if (!table)
    {
        caps.Set(capability, false, table.error().Detail);
        return;
    }
    target = std::move(*table);
}

Status Bindings::Bind(const GameData& data, Capabilities& caps)
{
    using Kind = GameData::Kind;

    if (data.Resolutions().empty())
        return std::unexpected(Error::NotReady("gamedata is empty; nothing to bind"));

    CreateEntityByName = Fn<CEntityInstance*(const char*, int)>(AddressOf(data, "CreateEntityByName"));
    DispatchSpawn = Fn<void(CEntityInstance*, CEntityKeyValues*)>(AddressOf(data, "DispatchSpawn"));
    AcceptInput = Fn<void(CEntityInstance*, const char*, CEntityInstance*, CEntityInstance*, void*, int, void*)>(
        AddressOf(data, "CEntityInstance_AcceptInput"));
    AddEntityIOEvent = Fn<void(void*, CEntityInstance*, const char*, CEntityInstance*, CEntityInstance*, void*, float,
                               int, void*, void*)>(AddressOf(data, "CEntitySystem_AddEntityIOEvent"));
    UtilRemove = Fn<void(CEntityInstance*)>(AddressOf(data, "UTIL_Remove"));
    SetModel = Fn<void(CEntityInstance*, const char*)>(AddressOf(data, "CBaseModelEntity_SetModel"));
    EmitSoundParams =
        Fn<void(CEntityInstance*, const char*, int, float, float)>(AddressOf(data, "CBaseEntity_EmitSoundParams"));
    EmitSoundFilter = Address(AddressOf(data, "CBaseEntity_EmitSoundFilter"));
    FindEntityByClassName = Fn<CEntityInstance*(void*, CEntityInstance*, const char*)>(
        AddressOf(data, "CGameEntitySystem_FindEntityByClassName"));
    FindEntityByName =
        Fn<CEntityInstance*(void*, CEntityInstance*, const char*, CEntityInstance*, CEntityInstance*, CEntityInstance*,
                            void*)>(AddressOf(data, "CGameEntitySystem_FindEntityByName"));
    LegacyGameEventListener = Address(AddressOf(data, "LegacyGameEventListener"));

    GameEventManager = Address(AddressOf(data, "GameEventManager"));
    GameSystemFactoryList = Address(AddressOf(data, "GameSystemFactoryList"));
    GameSystemEventDispatcher = Address(AddressOf(data, "GameSystemEventDispatcher"));
    GameSystemList = Address(AddressOf(data, "GameSystemList"));

    CommitSuicide = VFn<void(bool, bool)>(IndexOf(data, "CommitSuicide"));
    ChangeTeam = VFn<void(int)>(IndexOf(data, "ChangeTeam"));
    Respawn = VFn<void()>(IndexOf(data, "Respawn"));
    Teleport = VFn<void(const Vector*, const QAngle*, const Vector*)>(IndexOf(data, "Teleport"));
    RunCommand = VFn<void*(void*)>(IndexOf(data, "RunCommand"));
    GiveNamedItem = VFn<void*(const char*)>(IndexOf(data, "GiveNamedItem"));
    RemoveAllItems = VFn<void(bool)>(IndexOf(data, "RemoveAllItems"));
    ProcessRespondCvarValue = VFn<bool(const void*)>(IndexOf(data, "ProcessRespondCvarValue"));

    GameEntitySystem = OffsetOf<CGameEntitySystem*>(IndexOf(data, "GameEntitySystem"));
    CheckTransmitPlayerSlot = OffsetOf<uint8_t>(IndexOf(data, "CheckTransmitPlayerSlot"));
    ServerSideClientSlot = OffsetOf<int>(IndexOf(data, "ServerSideClientSlot"));
    UserCmdPB = OffsetOf<void>(IndexOf(data, "UserCmdPB"));
    UserCmdNumber = OffsetOf<int32_t>(IndexOf(data, "UserCmdNumber"));

    // Capabilities that gamedata alone decides. The rest (Schema, Menus, Vote, Http) are recorded
    // by Runtime::Start from their own setup.
    Gate(data, caps, Capability::Entities, {{"GameEntitySystem", Kind::Offset}});
    Gate(data, caps, Capability::EntityOps,
         {{"CreateEntityByName", Kind::Signature},
          {"DispatchSpawn", Kind::Signature},
          {"CEntityInstance_AcceptInput", Kind::Signature}});
    Gate(data, caps, Capability::GameEvents, {{"GameEventManager", Kind::Address}});
    Gate(data, caps, Capability::Precache,
         {{"GameSystemFactoryList", Kind::Address},
          {"GameSystemEventDispatcher", Kind::Address},
          {"GameSystemList", Kind::Address}});
    Gate(data, caps, Capability::Items, {{"GiveNamedItem", Kind::VTable}, {"RemoveAllItems", Kind::VTable}});
    Gate(data, caps, Capability::Teleport, {{"Teleport", Kind::VTable}});
    Gate(data, caps, Capability::Transmit, {{"CheckTransmitPlayerSlot", Kind::Offset}});
    // UserCmdNumber is not required: without it the decoder falls back to the protobuf counter.
    Gate(data, caps, Capability::Movement, {{"RunCommand", Kind::VTable}, {"UserCmdPB", Kind::Offset}});
    Gate(data, caps, Capability::ClientCvars,
         {{"ProcessRespondCvarValue", Kind::VTable}, {"ServerSideClientSlot", Kind::Offset}});

    // The two class vtables, each folded into the capability whose hook binds it.
    GateTable(data, caps, Capability::Movement, "RunCommand", MovementServices);
    GateTable(data, caps, Capability::ClientCvars, "ProcessRespondCvarValue", ServerSideClient);

    return {};
}

}  // namespace VoltMod
