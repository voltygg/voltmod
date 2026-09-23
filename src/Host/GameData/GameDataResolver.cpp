#include "Host/GameData/GameDataResolver.hpp"

#include "Core/Files/GameBuild.hpp"
#include "Engine/Memory/SigScanner.hpp"

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

static uint64_t Rva(const Image& module, const void* address)
{
    return static_cast<uint64_t>(static_cast<const uint8_t*>(address) - module.Base);
}

static Result<ScanResult> Scan(std::string_view moduleName, const std::string& pattern)
{
    if (pattern.empty())
    {
        return Unbound("empty pattern");
    }

    ScanResult match = FindPatternEx(moduleName, pattern);
    if (!match.Module.Base)
    {
        return Unbound(std::format("module '{}' is not loaded", moduleName));
    }
    if (!match.Address)
    {
        return Unbound("pattern not found");
    }
    if (!match.Unique)
    {
        return Unbound("pattern matched more than once");
    }
    return match;
}

/** A non-negative number from this platform's column, or why there is none. */
template <class TEntry>
static Result<int> ColumnValue(const TEntry& entry, std::string_view what)
{
    const auto& value = PlatformColumn(entry);
    if (!value)
    {
        return Unbound(std::format("no {} {}", PlatformName, what));
    }
    if (*value < 0)
    {
        return Unbound(std::format("{} {} is negative", what, *value));
    }
    return *value;
}

static const void* OriginalSlot(void* table, int index, const OriginalSlotLookup& originalOf)
{
    void** slots = static_cast<void**>(table);
    const void* original = originalOf ? originalOf(slots, index) : nullptr;
    return original ? original : slots[index];
}

GameDataResolver::GameDataResolver(const GameDataDocument& file, const OriginalSlotLookup& originalOf,
                                   const ScriptBindingLookup& scriptOf)
    : _file(file), _originalOf(originalOf), _scriptOf(scriptOf)
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
        {
            Fail(key, entry,
                 Error::NotFound(std::format("in both '{}' and '{}'", entry.Sections[0], entry.Sections[1])));
        }
    }
}

void GameDataResolver::ResolveAll()
{
    for (auto& [key, entry] : _entries)
    {
        if (!entry.Reason.empty())
        {
            continue;
        }
        if (const Result<Bound> bound = Locate(entry.Kind, key))
        {
            entry.Address = bound->Address;
            entry.Value = bound->Value;
        }
        else
        {
            Fail(key, entry, bound.error());
        }
    }
}

const ResolvedEntry* GameDataResolver::Find(std::string_view key) const
{
    const auto it = _entries.find(key);
    return it != _entries.end() ? &it->second : nullptr;
}

void GameDataResolver::LogSummary(std::string_view path) const
{
    Log::Info("GameData: {} (server {}, verified {}): {} functions, {} globals, {} vtables, {} offsets.", path,
              _file.build.server, _file.build.verified, _file.functions.size(), _file.globals.size(),
              _file.vtables.size(), _file.offsets.size());

    // Patterns, RTTI and VScript are checked at load; hand-written numbers are not.
    if (_file.build.server != GameBuild())
    {
        const auto indices =
            std::ranges::count_if(_file.vtables, [](const auto& each) { return each.second.Script.empty(); });
        const auto numbered =
            std::ranges::count_if(_file.offsets, [](const auto& each) { return each.second.Base.empty(); });
        Log::Warn("GameData: verified on server {}, running {}: {} vtable indices and {} offsets are unchecked.",
                  _file.build.server, GameBuild(), indices, numbered);
    }
}

Result<GameDataResolver::Bound> GameDataResolver::Locate(GameDataSection kind, const std::string& key)
{
    const auto address = [](void* found) { return Bound{.Address = found}; };
    switch (kind)
    {
    case GameDataSection::Function:
        return FindFunction(key).transform(address);
    case GameDataSection::Global:
        return FindGlobal(key).transform(address);
    case GameDataSection::VTable:
        return FindSlot(key).transform(
            [](VirtualSlot slot) { return Bound{.Address = slot.Table, .Value = slot.Index}; });
    case GameDataSection::Offset:
        return FindOffset(key).transform([](int value) { return Bound{.Value = value}; });
    }
    return Unbound("in no section");
}

void GameDataResolver::Fail(std::string_view key, ResolvedEntry& entry, const Error& error)
{
    entry.Reason = error.Detail;
    _failures.push_back(std::format("{}: {}", key, entry.Reason));
}

Result<void*> GameDataResolver::FindFunction(const std::string& key)
{
    const GameDataDocument::Function& entry = _file.functions.at(key);
    if (!entry.Script.empty())
    {
        return FindScriptFunction(key, entry);
    }

    const auto& pattern = PlatformColumn(entry);
    if (!pattern)
    {
        return Unbound(std::format("no {} pattern", PlatformName));
    }

    const auto match = Scan(entry.Module, *pattern);
    if (!match)
    {
        return std::unexpected(match.error());
    }

    _resolved.Functions.emplace(key, ResolvedGameData::Location{entry.Module, Rva(match->Module, match->Address)});
    return match->Address;
}

Result<void*> GameDataResolver::FindScriptFunction(const std::string& key, const GameDataDocument::Function& entry)
{
    const auto module = _cache.Module(entry.Module);
    if (!module)
    {
        return std::unexpected(module.error());
    }
    const auto table = _cache.Table(entry.Module, entry.Class);
    if (!table)
    {
        return std::unexpected(table.error());
    }
    const auto target = FindScript(*table, entry.Script);
    if (!target)
    {
        return std::unexpected(target.error());
    }
    if (!(*module)->Contains(target->Address))
    {
        return Unbound(std::format("VScript '{}' is virtual or outside '{}'", entry.Script, entry.Module));
    }

    _resolved.Functions.emplace(key, ResolvedGameData::Location{entry.Module, Rva(**module, target->Address)});
    return target->Address;
}

Result<void*> GameDataResolver::FindGlobal(const std::string& key)
{
    const GameDataDocument::Global& entry = _file.globals.at(key);
    const auto& column = PlatformColumn(entry);
    if (!column)
    {
        return Unbound(std::format("no {} pattern", PlatformName));
    }

    const auto match = Scan(entry.Module, column->pattern);
    if (!match)
    {
        return std::unexpected(match.error());
    }

    const uintptr_t target =
        ResolveRelativeAddress(match->Module, reinterpret_cast<uintptr_t>(match->Address), column->rel32At);
    auto* global = reinterpret_cast<void*>(target);
    if (!target || !match->Module.Contains(global) || !IsReadableAddress(global, sizeof(void*)))
    {
        return Unbound(
            std::format("the rel32 at +{} does not point at readable memory in '{}'", column->rel32At, entry.Module));
    }

    _resolved.Globals.emplace(key, ResolvedGameData::Location{entry.Module, Rva(match->Module, global)});
    return global;
}

Result<VirtualSlot> GameDataResolver::FindSlot(const std::string& key)
{
    const GameDataDocument::VTable& entry = _file.vtables.at(key);
    // A `script` entry's index needs the class table, so it is read once the table is found.
    Result<int> index = entry.Script.empty() ? ColumnValue(entry, "index") : -1;
    if (!index)
    {
        return std::unexpected(index.error());
    }

    const auto module = _cache.Module(entry.Module);
    if (!module)
    {
        return std::unexpected(module.error());
    }
    const auto table = _cache.Table(entry.Module, entry.Class, entry.Base);
    if (!table)
    {
        return std::unexpected(table.error());
    }
    if (!entry.Script.empty())
    {
        index = ScriptSlot(*table, entry.Script);
        if (!index)
        {
            return std::unexpected(index.error());
        }
    }

    // A short table may end before the index, so check the slot is readable first.
    void** slot = static_cast<void**>(*table) + *index;
    const void* code = IsReadableAddress(slot, sizeof(void*)) ? OriginalSlot(*table, *index, _originalOf) : nullptr;
    if (!IsExecutableAddress(code))
    {
        const std::string& owner = entry.Base.empty() ? entry.Class : entry.Base;
        return Unbound(std::format("{}::[{}] does not hold code", owner, *index));
    }

    // A hook trampoline can live outside the module, so it has no module-relative address.
    const Image& loaded = **module;
    const uint64_t codeRva = loaded.Contains(code) ? Rva(loaded, code) : 0;
    _resolved.VTables.emplace(key, ResolvedGameData::Slot{entry.Module, Rva(loaded, *table), *index, codeRva});
    return VirtualSlot{.Index = *index, .Table = *table};
}

Result<ScriptTarget> GameDataResolver::FindScript(void* table, const std::string& name) const
{
    if (!_scriptOf)
    {
        return Unbound("no VScript lookup");
    }
    return _scriptOf(table, name);
}

Result<int> GameDataResolver::ScriptSlot(void* table, const std::string& name) const
{
    const auto target = FindScript(table, name);
    if (!target)
    {
        return std::unexpected(target.error());
    }
    if (target->Index < 0)
    {
        return Unbound(std::format("VScript '{}' is not virtual", name));
    }
    return target->Index;
}

Result<int> GameDataResolver::FindOffset(const std::string& key)
{
    const GameDataDocument::Offset& entry = _file.offsets.at(key);
    Result<int> offset = entry.Base.empty() ? ColumnValue(entry, "offset") : FindBaseOffset(entry);
    if (offset)
    {
        _resolved.Offsets.emplace(key, *offset);
    }
    return offset;
}

Result<int> GameDataResolver::FindBaseOffset(const GameDataDocument::Offset& entry)
{
    if (entry.Class.empty())
    {
        return Unbound(std::format("base '{}' names no class", entry.Base));
    }
    return _cache.Base(entry.Module, entry.Class, entry.Base).transform([](const BaseSubobject& base) {
        return base.Offset;
    });
}

}  // namespace VoltMod
