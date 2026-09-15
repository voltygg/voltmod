#include "Core/GameBuild.hpp"
#include "Engine/GameData/GameDataDocument.hpp"
#include "Engine/Memory/SigScanner.hpp"
#include "Engine/Memory/VtableLookup.hpp"

#include <VoltMod/Core/File.hpp>
#include <VoltMod/Core/Json.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Strings.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <cstdint>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace VoltMod
{

static uint64_t Rva(const ModuleImage& image, const void* address)
{
    return static_cast<uint64_t>(static_cast<const uint8_t*>(address) - image.Base);
}

/** What @p table's @p index held before another plugin hooked it. The slot must be readable. */
static const void* OriginalSlot(void* table, int index, const OriginalVfn& originalOf)
{
    void** slots = static_cast<void**>(table);
    const void* original = originalOf ? originalOf(slots, index) : nullptr;
    return original ? original : slots[index];
}

/** Resolves the gamedata key each member names, recording every one that does not bind. */
class GameDataResolver
{
public:
    GameDataResolver(const GameDataDocument& file, const OriginalVfn& originalOf, std::vector<std::string>& failures)
        : _file(file), _originalOf(originalOf), _failures(failures)
    {
        AddKeys(file.functions, "functions");
        AddKeys(file.globals, "globals");
        AddKeys(file.vtables, "vtables");
        AddKeys(file.offsets, "offsets");
    }

    template <class Sig>
    void operator()(Fn<Sig>& member, std::string_view key)
    {
        if (Claim(key, "functions"))
            member = Fn<Sig>(Function(key));
    }

    /** An ABI declared only in its implementation file: a function or a global. */
    void operator()(Address& member, std::string_view key)
    {
        if (const auto section = Claim(key, "functions", "globals"))
            member = Address(*section == "functions" ? Function(key) : Global(key));
    }

    template <class Sig>
    void operator()(VirtualFn<Sig>& member, std::string_view key)
    {
        if (!Claim(key, "vtables"))
            return;

        const BoundSlot slot = FindSlot(key);
        member = VirtualFn<Sig>(slot.Index, slot.Table);
    }

    template <class T>
    void operator()(OffsetOf<T>& member, std::string_view key)
    {
        if (Claim(key, "offsets"))
            member = OffsetOf<T>(Offset(key));
    }

    /** Keys no member binds. */
    std::vector<std::string> UnusedKeys() const
    {
        std::vector<std::string> unused;
        for (const auto& [key, sections] : _sections)
        {
            if (!_used.contains(key))
                unused.push_back(key);
        }
        return unused;
    }

    /** `key=library+offset` for each slot whose code is inside its module, for crash triage. */
    const std::vector<std::string>& SlotAddresses() const { return _slotAddresses; }

    /** Module-relative addresses of everything that bound. */
    const ResolvedRecord& Record() const { return _record; }

private:
    struct BoundSlot
    {
        int Index = -1;
        void* Table = nullptr;
    };

    template <class TEntry>
    void AddKeys(const std::map<std::string, TEntry>& entries, std::string_view section)
    {
        for (const auto& [key, entry] : entries)
            _sections[key].push_back(section);
    }

    /** The section @p key binds from, when it is in exactly one section this member reads. */
    std::optional<std::string_view> Claim(std::string_view key, std::string_view section,
                                          std::string_view alternative = {})
    {
        const auto it = _sections.find(key);
        if (it == _sections.end())
        {
            Fail(std::format("'{}' is not in gamedata", key));
            return std::nullopt;
        }

        _used.insert(it->first);
        const std::vector<std::string_view>& sections = it->second;
        if (sections.size() > 1)
            Fail(std::format("'{}' is in both '{}' and '{}'", key, sections[0], sections[1]));
        else if (sections[0] != section && sections[0] != alternative)
            Fail(std::format("'{}' is in '{}', which this member does not bind from", key, sections[0]));
        else
            return sections[0];
        return std::nullopt;
    }

    void Fail(std::string reason) { _failures.push_back(std::move(reason)); }
    void Fail(std::string_view key, std::string_view reason) { Fail(std::format("{}: {}", key, reason)); }

    void* Function(std::string_view key)
    {
        const GameDataDocument::Function& entry = _file.functions.at(std::string(key));
        const auto& pattern = PlatformColumn(entry);
        if (!pattern)
        {
            Fail(key, std::format("no {} pattern", PlatformName));
            return nullptr;
        }

        const std::optional<ScanResult> scan = Scan(key, entry.library, *pattern);
        if (!scan)
            return nullptr;

        _record.functions.emplace(std::string(key),
                                  ResolvedRecord::Location{entry.library, Rva(scan->Image, scan->Address)});
        return scan->Address;
    }

    void* Global(std::string_view key)
    {
        const GameDataDocument::Global& entry = _file.globals.at(std::string(key));
        const auto& column = PlatformColumn(entry);
        if (!column)
        {
            Fail(key, std::format("no {} pattern", PlatformName));
            return nullptr;
        }

        const std::optional<ScanResult> scan = Scan(key, entry.library, column->pattern);
        if (!scan)
            return nullptr;

        const uintptr_t target =
            ResolveRelativeAddress(scan->Image, reinterpret_cast<uintptr_t>(scan->Address), column->rel32At);
        auto* global = reinterpret_cast<void*>(target);
        if (!target || !scan->Image.Contains(global) || !IsReadableAddress(global, sizeof(void*)))
        {
            Fail(key, std::format("the rel32 at +{} does not point at readable memory in '{}'", column->rel32At,
                                  entry.library));
            return nullptr;
        }

        _record.globals.emplace(std::string(key), ResolvedRecord::Location{entry.library, Rva(scan->Image, global)});
        return global;
    }

    std::optional<ScanResult> Scan(std::string_view key, const std::string& library, const std::string& pattern)
    {
        if (pattern.empty())
            Fail(key, "empty pattern");
        else if (ScanResult scan = FindPatternEx(library.c_str(), pattern); !scan.Image.Base)
            Fail(key, std::format("module '{}' is not loaded", library));
        else if (!scan.Address)
            Fail(key, "pattern not found");
        else if (!scan.Unique)
            Fail(key, "pattern matched more than once");
        else
            return scan;
        return std::nullopt;
    }

    BoundSlot FindSlot(std::string_view key)
    {
        const GameDataDocument::VTable& entry = _file.vtables.at(std::string(key));
        const auto& index = PlatformColumn(entry);
        const ModuleImage* image = index ? Image(entry.library) : nullptr;
        void* table = image && *index >= 0 ? Table(*image, entry.library, entry.Class) : nullptr;

        if (!index)
            Fail(key, std::format("no {} index", PlatformName));
        else if (*index < 0)
            Fail(key, std::format("index {} is negative", *index));
        else if (!image)
            Fail(key, std::format("module '{}' is not loaded", entry.library));
        else if (!table)
            Fail(key, std::format("no vtable for '{}' in '{}'", entry.Class, entry.library));
        // A short table ends before the index, so the slot is checked before it is read.
        else if (void** slot = static_cast<void**>(table) + *index;
                 !IsReadableAddress(slot, sizeof(void*)) ||
                 !IsExecutableAddress(OriginalSlot(table, *index, _originalOf)))
            Fail(key, std::format("{}::[{}] does not hold code", entry.Class, *index));
        else
        {
            // Hook trampolines live outside the module and have no useful module offset.
            if (const void* code = OriginalSlot(table, *index, _originalOf); image->Contains(code))
                _slotAddresses.push_back(std::format("{}={}+{:#x}", key, entry.library, Rva(*image, code)));
            _record.vtables.emplace(std::string(key), ResolvedRecord::Slot{entry.library, Rva(*image, table), *index});
            return {.Index = *index, .Table = table};
        }
        return {};
    }

    int Offset(std::string_view key)
    {
        const auto& value = PlatformColumn(_file.offsets.at(std::string(key)));
        if (!value)
            Fail(key, std::format("no {} offset", PlatformName));
        else if (*value < 0)
            Fail(key, std::format("offset {} is negative", *value));
        else
        {
            _record.offsets.emplace(std::string(key), *value);
            return *value;
        }
        return -1;
    }

    /** One image per library, looked up once. Null when the module is not loaded. */
    const ModuleImage* Image(const std::string& library)
    {
        auto [it, added] = _images.try_emplace(library);
        if (added)
            FindModuleImage(library.c_str(), it->second);
        return it->second.Base ? &it->second : nullptr;
    }

    /** One table per library and class, located once; several slots share it. */
    void* Table(const ModuleImage& image, const std::string& library, const std::string& className)
    {
        auto [it, added] = _tables.try_emplace({library, className}, nullptr);
        if (added)
            it->second = FindVirtualTableIn(image, className.c_str());
        return it->second;
    }

    const GameDataDocument& _file;
    const OriginalVfn& _originalOf;
    std::vector<std::string>& _failures;
    std::map<std::string, std::vector<std::string_view>, std::less<>> _sections;
    std::set<std::string, std::less<>> _used;
    std::map<std::string, ModuleImage> _images;
    std::map<std::pair<std::string, std::string>, void*> _tables;
    std::vector<std::string> _slotAddresses;
    ResolvedRecord _record;
};

/** Keep what this build resolved, once per server build; other plugins on the same build skip it. */
static void WriteResolvedRecord(ResolvedRecord record)
{
    const std::string path = std::format("addons/voltmod/gamedata/resolved.{}.json", PlatformName);
    record.build = std::string(GameBuild());
    if (const auto existing = Json::ReadFile<ResolvedRecord>(path); existing && existing->build == record.build)
        return;

    if (const Status written = WriteAllText(path, Json::WritePretty(record)); !written)
        Log::Warn("GameData: no record written to {}: {}", path, written.error().Detail);
    else
        Log::Info("GameData: recorded what resolved on server {} in {}.", record.build, path);
}

Status Bindings::Load(std::string_view path, const OriginalVfn& originalOf)
{
    *this = Bindings{};

    auto file = Json::ReadFile<GameDataDocument, Json::StrictReadOptions>(path);
    if (!file)
        return std::unexpected(file.error());

    GameDataResolver bind(*file, originalOf, Failures);

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

    Log::Info("GameData: {} (server {}, verified {}): {} functions, {} globals, {} vtables, {} offsets.", path,
              file->build.server, file->build.verified, file->functions.size(), file->globals.size(),
              file->vtables.size(), file->offsets.size());
    if (!bind.SlotAddresses().empty())
        Log::Info("GameData: vtable slots hold {}.", Strings::Join(bind.SlotAddresses(), ", "));
    if (const std::vector<std::string> unused = bind.UnusedKeys(); !unused.empty())
        Log::Warn("GameData: {} entries bind to nothing: {}.", unused.size(), Strings::Join(unused, ", "));
    // A pattern proves itself by matching once; an index or offset cannot.
    if (file->build.server != GameBuild())
        Log::Warn("GameData: verified on server {}, running {}: {} vtable indices and {} offsets are unchecked.",
                  file->build.server, GameBuild(), file->vtables.size(), file->offsets.size());

    if (!Failures.empty())
        return std::unexpected(
            Error::Engine(std::format("{} did not bind: {}", Failures.size(), Strings::Join(Failures, "; "))));

    WriteResolvedRecord(bind.Record());
    return {};
}

}  // namespace VoltMod
