#include "Host/GameData/GameDataResolver.hpp"

#include "Core/Files/GameBuild.hpp"
#include "Engine/Memory/SigScanner.hpp"
#include "Engine/Memory/VtableLookup.hpp"

#include <VoltMod/Core/Log.hpp>
#include <algorithm>
#include <cstdint>
#include <format>
#include <ranges>

namespace VoltMod
{

static std::unexpected<Error> Unbound(std::string reason)
{
    return std::unexpected(Error::NotFound(std::move(reason)));
}

static uint64_t Rva(const LoadedModule& module, const void* address)
{
    return static_cast<uint64_t>(static_cast<const uint8_t*>(address) - module.Base);
}

static Result<ScanResult> Scan(std::string_view moduleName, const std::string& pattern)
{
    if (pattern.empty())
        return Unbound("empty pattern");

    ScanResult match = FindPatternEx(moduleName, pattern);
    if (!match.Module.Base)
        return Unbound(std::format("module '{}' is not loaded", moduleName));
    if (!match.Address)
        return Unbound("pattern not found");
    if (!match.Unique)
        return Unbound("pattern matched more than once");
    return match;
}

static const void* OriginalSlot(void* table, int index, const OriginalSlotLookup& originalOf)
{
    void** slots = static_cast<void**>(table);
    const void* original = originalOf ? originalOf(slots, index) : nullptr;
    return original ? original : slots[index];
}

const LoadedModule* ModuleCache::Module(const std::string& moduleName)
{
    auto [it, added] = _modules.try_emplace(moduleName);
    if (added)
        FindLoadedModule(moduleName, it->second);
    return it->second.Base ? &it->second : nullptr;
}

void* ModuleCache::ClassTable(const LoadedModule& module, const std::string& moduleName, const std::string& className)
{
    auto [it, added] = _tables.try_emplace({moduleName, className}, nullptr);
    if (added)
        it->second = FindVirtualTableIn(module, className);
    return it->second;
}

Result<BaseSubobject> ModuleCache::Base(const LoadedModule& module, const std::string& moduleName,
                                        const std::string& className, const std::string& baseName)
{
    auto key = std::tuple{moduleName, className, baseName};
    auto it = _bases.find(key);
    if (it == _bases.end())
        it = _bases.emplace(std::move(key), FindBaseIn(module, className, baseName)).first;
    return it->second;
}

GameDataResolver::GameDataResolver(const GameDataDocument& file, const OriginalSlotLookup& originalOf)
    : _file(file), _originalOf(originalOf)
{
    const auto index = [this](const auto& section, GameDataSection kind, std::string_view name) {
        for (const auto& key : section | std::views::keys)
        {
            ResolvedEntry& entry = _entries[key];
            entry.Sections.push_back(name);
            // A key in two sections binds from neither, so it has no kind to resolve from.
            entry.Kind = entry.Sections.size() == 1 ? kind : GameDataSection{};
        }
    };
    index(file.functions, GameDataSection::Function, "functions");
    index(file.globals, GameDataSection::Global, "globals");
    index(file.vtables, GameDataSection::VTable, "vtables");
    index(file.offsets, GameDataSection::Offset, "offsets");

    for (auto& [key, entry] : _entries)
    {
        if (entry.Sections.size() > 1)
            Fail(key, entry,
                 Error::NotFound(std::format("in both '{}' and '{}'", entry.Sections[0], entry.Sections[1])));
    }
}

void GameDataResolver::ResolveAll()
{
    for (auto& [key, entry] : _entries)
    {
        if (entry.Reason.empty())
            Resolve(key, entry);
    }
}

const ResolvedEntry* GameDataResolver::Find(std::string_view key) const
{
    const auto it = _entries.find(key);
    return it != _entries.end() ? &it->second : nullptr;
}

void GameDataResolver::Resolve(const std::string& key, ResolvedEntry& entry)
{
    switch (entry.Kind)
    {
    case GameDataSection::Function:
        if (const auto found = FindFunction(key))
            entry.Address = *found;
        else
            Fail(key, entry, found.error());
        return;

    case GameDataSection::Global:
        if (const auto found = FindGlobal(key))
            entry.Address = *found;
        else
            Fail(key, entry, found.error());
        return;

    case GameDataSection::VTable:
        if (const auto found = FindSlot(key))
        {
            entry.Address = found->Table;
            entry.Value = found->Index;
        }
        else
        {
            Fail(key, entry, found.error());
        }
        return;

    case GameDataSection::Offset:
        if (const auto found = FindOffset(key))
            entry.Value = *found;
        else
            Fail(key, entry, found.error());
        return;
    }
}

void GameDataResolver::Fail(std::string_view key, ResolvedEntry& entry, const Error& error)
{
    entry.Reason = error.Detail;
    _failures.push_back(std::format("{}: {}", key, entry.Reason));
}

void GameDataResolver::LogSummary(std::string_view path) const
{
    Log::Info("GameData: {} (server {}, verified {}): {} functions, {} globals, {} vtables, {} offsets.", path,
              _file.build.server, _file.build.verified, _file.functions.size(), _file.globals.size(),
              _file.vtables.size(), _file.offsets.size());

    // Patterns and RTTI are checked at load; hand-maintained indices and offsets are not.
    if (_file.build.server != GameBuild())
    {
        const auto numbered =
            std::ranges::count_if(_file.offsets, [](const auto& each) { return each.second.Base.empty(); });
        Log::Warn("GameData: verified on server {}, running {}: {} vtable indices and {} offsets are unchecked.",
                  _file.build.server, GameBuild(), _file.vtables.size(), numbered);
    }
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

    _resolved.Functions.emplace(key, ResolvedGameData::Location{entry.Module, Rva(match->Module, match->Address)});
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

    _resolved.Globals.emplace(key, ResolvedGameData::Location{entry.Module, Rva(match->Module, global)});
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

    const LoadedModule* module = _cache.Module(entry.Module);
    if (!module)
        return Unbound(std::format("module '{}' is not loaded", entry.Module));

    void* table = nullptr;
    std::string_view owner = entry.Class;
    if (entry.Base.empty())
    {
        table = _cache.ClassTable(*module, entry.Module, entry.Class);
        if (!table)
            return Unbound(std::format("no vtable for '{}' in '{}'", entry.Class, entry.Module));
    }
    else
    {
        const auto base = _cache.Base(*module, entry.Module, entry.Class, entry.Base);
        if (!base)
            return std::unexpected(base.error());
        if (!base->Table)
            return Unbound(std::format("'{}' has no vtable of its own in '{}'", entry.Base, entry.Class));
        table = base->Table;
        owner = entry.Base;
    }

    // Check the slot before reading it because a short table may end before the index.
    void** slot = static_cast<void**>(table) + *index;
    const void* code = IsReadableAddress(slot, sizeof(void*)) ? OriginalSlot(table, *index, _originalOf) : nullptr;
    if (!IsExecutableAddress(code))
        return Unbound(std::format("{}::[{}] does not hold code", owner, *index));

    // A hook trampoline can live outside the module, so it has no module-relative address.
    const uint64_t codeRva = module->Contains(code) ? Rva(*module, code) : 0;
    _resolved.VTables.emplace(key, ResolvedGameData::Slot{entry.Module, Rva(*module, table), *index, codeRva});
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

    _resolved.Offsets.emplace(key, *value);
    return *value;
}

Result<int> GameDataResolver::FindBaseOffset(const std::string& key, const GameDataDocument::Offset& entry)
{
    if (entry.Class.empty())
        return Unbound(std::format("base '{}' names no class", entry.Base));

    const LoadedModule* module = _cache.Module(entry.Module);
    if (!module)
        return Unbound(std::format("module '{}' is not loaded", entry.Module));

    const auto base = _cache.Base(*module, entry.Module, entry.Class, entry.Base);
    if (!base)
        return std::unexpected(base.error());

    _resolved.Offsets.emplace(key, base->Offset);
    return base->Offset;
}

}  // namespace VoltMod
