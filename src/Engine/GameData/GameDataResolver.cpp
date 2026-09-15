#include "Engine/GameData/GameDataResolver.hpp"

#include "Core/GameBuild.hpp"
#include "Engine/Memory/SigScanner.hpp"
#include "Engine/Memory/VtableLookup.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Strings.hpp>
#include <algorithm>
#include <cstdint>
#include <format>

namespace VoltMod
{

static std::unexpected<Error> Unbound(std::string reason)
{
    return std::unexpected(Error::NotFound(std::move(reason)));
}

static uint64_t Rva(const LoadedModule& loaded, const void* address)
{
    return static_cast<uint64_t>(static_cast<const uint8_t*>(address) - loaded.Base);
}

/** The single match of @p pattern in @p moduleName. */
static Result<ScanResult> Scan(const std::string& moduleName, const std::string& pattern)
{
    if (pattern.empty())
        return Unbound("empty pattern");

    ScanResult match = FindPatternEx(moduleName.c_str(), pattern);
    if (!match.Module.Base)
        return Unbound(std::format("module '{}' is not loaded", moduleName));
    if (!match.Address)
        return Unbound("pattern not found");
    if (!match.Unique)
        return Unbound("pattern matched more than once");
    return match;
}

/** What @p table's @p index held before another plugin hooked it. The slot must be readable. */
static const void* OriginalSlot(void* table, int index, const OriginalVfn& originalOf)
{
    void** slots = static_cast<void**>(table);
    const void* original = originalOf ? originalOf(slots, index) : nullptr;
    return original ? original : slots[index];
}

GameDataResolver::GameDataResolver(const GameDataDocument& file, const OriginalVfn& originalOf)
    : _file(file), _originalOf(originalOf)
{
    for (const auto& [key, entry] : file.functions)
        _sections[key].push_back("functions");
    for (const auto& [key, entry] : file.globals)
        _sections[key].push_back("globals");
    for (const auto& [key, entry] : file.vtables)
        _sections[key].push_back("vtables");
    for (const auto& [key, entry] : file.offsets)
        _sections[key].push_back("offsets");
}

void* GameDataResolver::Function(std::string_view key)
{
    const auto found =
        Claim(key, {"functions"}).and_then([&](std::string_view) { return FindFunction(std::string(key)); });
    return Keep(key, found);
}

void* GameDataResolver::FunctionOrGlobal(std::string_view key)
{
    const auto found = Claim(key, {"functions", "globals"}).and_then([&](std::string_view section) {
        return section == "functions" ? FindFunction(std::string(key)) : FindGlobal(std::string(key));
    });
    return Keep(key, found);
}

VirtualSlot GameDataResolver::Slot(std::string_view key)
{
    const auto found = Claim(key, {"vtables"}).and_then([&](std::string_view) { return FindSlot(std::string(key)); });
    return Keep(key, found);
}

int GameDataResolver::Offset(std::string_view key)
{
    const auto found = Claim(key, {"offsets"}).and_then([&](std::string_view) { return FindOffset(std::string(key)); });
    return Keep(key, found, -1);
}

void GameDataResolver::LogSummary(std::string_view path) const
{
    Log::Info("GameData: {} (server {}, verified {}): {} functions, {} globals, {} vtables, {} offsets.", path,
              _file.build.server, _file.build.verified, _file.functions.size(), _file.globals.size(),
              _file.vtables.size(), _file.offsets.size());
    if (!_slotAddresses.empty())
        Log::Info("GameData: vtable slots hold {}.", Strings::Join(_slotAddresses, ", "));
    if (!_baseOffsets.empty())
        Log::Info("GameData: RTTI placed {}.", Strings::Join(_baseOffsets, ", "));

    std::vector<std::string> unused;
    for (const auto& [key, sections] : _sections)
    {
        if (!_used.contains(key))
            unused.push_back(key);
    }
    if (!unused.empty())
        Log::Warn("GameData: {} entries bind to nothing: {}.", unused.size(), Strings::Join(unused, ", "));

    // A pattern or RTTI proves itself at load; a hand-kept index or offset cannot.
    if (_file.build.server != GameBuild())
    {
        const auto numbered =
            std::ranges::count_if(_file.offsets, [](const auto& each) { return each.second.Base.empty(); });
        Log::Warn("GameData: verified on server {}, running {}: {} vtable indices and {} offsets are unchecked.",
                  _file.build.server, GameBuild(), _file.vtables.size(), numbered);
    }
}

Result<std::string_view> GameDataResolver::Claim(std::string_view key, std::initializer_list<std::string_view> readable)
{
    const auto it = _sections.find(key);
    if (it == _sections.end())
        return Unbound("not in gamedata");

    _used.insert(it->first);
    const std::vector<std::string_view>& sections = it->second;
    if (sections.size() > 1)
        return Unbound(std::format("in both '{}' and '{}'", sections[0], sections[1]));
    if (!std::ranges::contains(readable, sections[0]))
        return Unbound(std::format("in '{}', which this member does not bind from", sections[0]));
    return sections[0];
}

Result<void*> GameDataResolver::FindFunction(const std::string& key)
{
    const GameDataDocument::Function& entry = _file.functions.at(key);
    const auto& pattern = PlatformColumn(entry);
    if (!pattern)
        return Unbound(std::format("no {} pattern", PlatformName));

    const auto match = Scan(entry.Module, *pattern);
    if (!match)
        return std::unexpected(match.error());

    _record.Functions.emplace(key, ResolvedRecord::Location{entry.Module, Rva(match->Module, match->Address)});
    return match->Address;
}

Result<void*> GameDataResolver::FindGlobal(const std::string& key)
{
    const GameDataDocument::Global& entry = _file.globals.at(key);
    const auto& column = PlatformColumn(entry);
    if (!column)
        return Unbound(std::format("no {} pattern", PlatformName));

    const auto match = Scan(entry.Module, column->pattern);
    if (!match)
        return std::unexpected(match.error());

    const uintptr_t target =
        ResolveRelativeAddress(match->Module, reinterpret_cast<uintptr_t>(match->Address), column->rel32At);
    auto* global = reinterpret_cast<void*>(target);
    if (!target || !match->Module.Contains(global) || !IsReadableAddress(global, sizeof(void*)))
        return Unbound(
            std::format("the rel32 at +{} does not point at readable memory in '{}'", column->rel32At, entry.Module));

    _record.Globals.emplace(key, ResolvedRecord::Location{entry.Module, Rva(match->Module, global)});
    return global;
}

Result<VirtualSlot> GameDataResolver::FindSlot(const std::string& key)
{
    const GameDataDocument::VTable& entry = _file.vtables.at(key);
    const auto& index = PlatformColumn(entry);
    if (!index)
        return Unbound(std::format("no {} index", PlatformName));
    if (*index < 0)
        return Unbound(std::format("index {} is negative", *index));

    const LoadedModule* loaded = FindModule(entry.Module);
    if (!loaded)
        return Unbound(std::format("module '{}' is not loaded", entry.Module));

    void* table = nullptr;
    if (entry.Base.empty())
        table = Table(*loaded, entry.Module, entry.Class);
    else if (const auto base = FindBase(*loaded, entry.Module, entry.Class, entry.Base); !base)
        return std::unexpected(base.error());
    else
        table = base->Table;

    if (!table && entry.Base.empty())
        return Unbound(std::format("no vtable for '{}' in '{}'", entry.Class, entry.Module));
    if (!table)
        return Unbound(std::format("'{}' has no vtable of its own in '{}'", entry.Base, entry.Class));

    // A short table ends before the index, so the slot is checked before it is read.
    void** slot = static_cast<void**>(table) + *index;
    const void* code = IsReadableAddress(slot, sizeof(void*)) ? OriginalSlot(table, *index, _originalOf) : nullptr;
    if (!IsExecutableAddress(code))
        return Unbound(
            std::format("{}::[{}] does not hold code", entry.Base.empty() ? entry.Class : entry.Base, *index));

    // Hook trampolines live outside the module and have no useful module offset.
    if (loaded->Contains(code))
        _slotAddresses.push_back(std::format("{}={}+{:#x}", key, entry.Module, Rva(*loaded, code)));
    _record.VTables.emplace(key, ResolvedRecord::Slot{entry.Module, Rva(*loaded, table), *index});
    return VirtualSlot{.Index = *index, .Table = table};
}

Result<int> GameDataResolver::FindOffset(const std::string& key)
{
    const GameDataDocument::Offset& entry = _file.offsets.at(key);
    if (!entry.Base.empty())
        return FindBaseOffset(key, entry);

    const auto& value = PlatformColumn(entry);
    if (!value)
        return Unbound(std::format("no {} offset", PlatformName));
    if (*value < 0)
        return Unbound(std::format("offset {} is negative", *value));

    _record.Offsets.emplace(key, *value);
    return *value;
}

Result<int> GameDataResolver::FindBaseOffset(const std::string& key, const GameDataDocument::Offset& entry)
{
    if (entry.Class.empty())
        return Unbound(std::format("base '{}' names no class", entry.Base));

    const LoadedModule* loaded = FindModule(entry.Module);
    if (!loaded)
        return Unbound(std::format("module '{}' is not loaded", entry.Module));

    const auto base = FindBase(*loaded, entry.Module, entry.Class, entry.Base);
    if (!base)
        return std::unexpected(base.error());

    _baseOffsets.push_back(std::format("{} at +{}", key, base->Offset));
    _record.Offsets.emplace(key, base->Offset);
    return base->Offset;
}

const LoadedModule* GameDataResolver::FindModule(const std::string& moduleName)
{
    auto [it, added] = _modules.try_emplace(moduleName);
    if (added)
        FindLoadedModule(moduleName.c_str(), it->second);
    return it->second.Base ? &it->second : nullptr;
}

void* GameDataResolver::Table(const LoadedModule& loaded, const std::string& moduleName, const std::string& className)
{
    auto [it, added] = _tables.try_emplace({moduleName, className}, nullptr);
    if (added)
        it->second = FindVirtualTableIn(loaded, className.c_str());
    return it->second;
}

Result<BaseSubobject> GameDataResolver::FindBase(const LoadedModule& loaded, const std::string& moduleName,
                                                 const std::string& className, const std::string& baseName)
{
    auto key = std::tuple{moduleName, className, baseName};
    auto it = _bases.find(key);
    if (it == _bases.end())
        it = _bases.emplace(std::move(key), FindBaseIn(loaded, className.c_str(), baseName.c_str())).first;
    return it->second;
}

template <class T>
T GameDataResolver::Keep(std::string_view key, const Result<T>& resolved, std::type_identity_t<T> unbound)
{
    if (resolved)
        return *resolved;

    _failures.push_back(std::format("{}: {}", key, resolved.error().Detail));
    return unbound;
}

}  // namespace VoltMod
