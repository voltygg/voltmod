#include "Engine/GameData/GameDataResolver.hpp"

#include "Core/Files/GameBuild.hpp"
#include "Engine/Memory/SigScanner.hpp"
#include "Engine/Memory/VtableLookup.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Text/Strings.hpp>
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
    const auto index = [this](const auto& section, std::string_view name) {
        for (const auto& key : section | std::views::keys)
            _sections[key].Sections.push_back(name);
    };
    index(file.functions, "functions");
    index(file.globals, "globals");
    index(file.vtables, "vtables");
    index(file.offsets, "offsets");
}

void* GameDataResolver::Function(std::string_view key)
{
    const auto found = UseKey(key, {"functions"}).and_then([&] { return FindFunction(std::string(key)); });
    return Bind(key, found);
}

void* GameDataResolver::FunctionOrGlobal(std::string_view key)
{
    const auto found = UseKey(key, {"functions", "globals"}).and_then([&] {
        std::string name(key);
        return _file.functions.contains(name) ? FindFunction(name) : FindGlobal(name);
    });
    return Bind(key, found);
}

VirtualSlot GameDataResolver::Slot(std::string_view key)
{
    const auto found = UseKey(key, {"vtables"}).and_then([&] { return FindSlot(std::string(key)); });
    return Bind(key, found);
}

int GameDataResolver::Offset(std::string_view key)
{
    const auto found = UseKey(key, {"offsets"}).and_then([&] { return FindOffset(std::string(key)); });
    return Bind(key, found, -1);
}

void GameDataResolver::LogSummary(std::string_view path) const
{
    Log::Info("GameData: {} (server {}, verified {}): {} functions, {} globals, {} vtables, {} offsets.", path,
              _file.build.server, _file.build.verified, _file.functions.size(), _file.globals.size(),
              _file.vtables.size(), _file.offsets.size());
    if (!_slotAddresses.empty())
        Log::Info("GameData: vtable slots hold {}.", Strings::Join(_slotAddresses, ", "));

    std::vector<std::string> baseOffsets;
    for (const auto& [key, entry] : _file.offsets)
    {
        const auto placed = entry.Base.empty() ? _record.Offsets.end() : _record.Offsets.find(key);
        if (placed != _record.Offsets.end())
            baseOffsets.push_back(std::format("{} at +{}", key, placed->second));
    }
    if (!baseOffsets.empty())
        Log::Info("GameData: RTTI placed {}.", Strings::Join(baseOffsets, ", "));

    std::vector<std::string> unused;
    for (const auto& [key, entry] : _sections)
    {
        if (!entry.Used)
            unused.push_back(key);
    }
    if (!unused.empty())
        Log::Warn("GameData: {} entries bind to nothing: {}.", unused.size(), Strings::Join(unused, ", "));

    // Patterns and RTTI are checked at load; hand-maintained indices and offsets are not.
    if (_file.build.server != GameBuild())
    {
        const auto numbered =
            std::ranges::count_if(_file.offsets, [](const auto& each) { return each.second.Base.empty(); });
        Log::Warn("GameData: verified on server {}, running {}: {} vtable indices and {} offsets are unchecked.",
                  _file.build.server, GameBuild(), _file.vtables.size(), numbered);
    }
}

Status GameDataResolver::UseKey(std::string_view key, std::initializer_list<std::string_view> allowed)
{
    const auto it = _sections.find(key);
    if (it == _sections.end())
        return Unbound("not in gamedata");

    it->second.Used = true;
    const std::vector<std::string_view>& sections = it->second.Sections;
    if (sections.size() > 1)
        return Unbound(std::format("in both '{}' and '{}'", sections[0], sections[1]));
    if (!std::ranges::contains(allowed, sections[0]))
        return Unbound(std::format("in '{}', which this member does not bind from", sections[0]));
    return {};
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

    // Hook trampolines can live outside the module, so they have no module-relative address.
    if (module->Contains(code))
        _slotAddresses.push_back(std::format("{}={}+{:#x}", key, entry.Module, Rva(*module, code)));
    _record.VTables.emplace(key, ResolvedRecord::Slot{entry.Module, Rva(*module, table), *index});
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

    const LoadedModule* module = _cache.Module(entry.Module);
    if (!module)
        return Unbound(std::format("module '{}' is not loaded", entry.Module));

    const auto base = _cache.Base(*module, entry.Module, entry.Class, entry.Base);
    if (!base)
        return std::unexpected(base.error());

    _record.Offsets.emplace(key, base->Offset);
    return base->Offset;
}

template <class T>
T GameDataResolver::Bind(std::string_view key, const Result<T>& resolved, std::type_identity_t<T> unbound)
{
    if (resolved)
        return *resolved;

    _failures.push_back(std::format("{}: {}", key, resolved.error().Detail));
    return unbound;
}

}  // namespace VoltMod
