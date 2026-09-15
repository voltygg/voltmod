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

static uint64_t Rva(const ModuleImage& image, const void* address)
{
    return static_cast<uint64_t>(static_cast<const uint8_t*>(address) - image.Base);
}

/** The single match of @p pattern in @p library. */
static Result<ScanResult> Scan(const std::string& library, const std::string& pattern)
{
    if (pattern.empty())
        return Unbound("empty pattern");

    ScanResult match = FindPatternEx(library.c_str(), pattern);
    if (!match.Image.Base)
        return Unbound(std::format("module '{}' is not loaded", library));
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

    std::vector<std::string> unused;
    for (const auto& [key, sections] : _sections)
    {
        if (!_used.contains(key))
            unused.push_back(key);
    }
    if (!unused.empty())
        Log::Warn("GameData: {} entries bind to nothing: {}.", unused.size(), Strings::Join(unused, ", "));

    // A pattern proves itself by matching once; an index or offset cannot.
    if (_file.build.server != GameBuild())
        Log::Warn("GameData: verified on server {}, running {}: {} vtable indices and {} offsets are unchecked.",
                  _file.build.server, GameBuild(), _file.vtables.size(), _file.offsets.size());
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

    const auto match = Scan(entry.library, *pattern);
    if (!match)
        return std::unexpected(match.error());

    _record.functions.emplace(key, ResolvedRecord::Location{entry.library, Rva(match->Image, match->Address)});
    return match->Address;
}

Result<void*> GameDataResolver::FindGlobal(const std::string& key)
{
    const GameDataDocument::Global& entry = _file.globals.at(key);
    const auto& column = PlatformColumn(entry);
    if (!column)
        return Unbound(std::format("no {} pattern", PlatformName));

    const auto match = Scan(entry.library, column->pattern);
    if (!match)
        return std::unexpected(match.error());

    const uintptr_t target =
        ResolveRelativeAddress(match->Image, reinterpret_cast<uintptr_t>(match->Address), column->rel32At);
    auto* global = reinterpret_cast<void*>(target);
    if (!target || !match->Image.Contains(global) || !IsReadableAddress(global, sizeof(void*)))
        return Unbound(
            std::format("the rel32 at +{} does not point at readable memory in '{}'", column->rel32At, entry.library));

    _record.globals.emplace(key, ResolvedRecord::Location{entry.library, Rva(match->Image, global)});
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

    const ModuleImage* image = Image(entry.library);
    if (!image)
        return Unbound(std::format("module '{}' is not loaded", entry.library));

    void* table = Table(*image, entry.library, entry.Class);
    if (!table)
        return Unbound(std::format("no vtable for '{}' in '{}'", entry.Class, entry.library));

    // A short table ends before the index, so the slot is checked before it is read.
    void** slot = static_cast<void**>(table) + *index;
    const void* code = IsReadableAddress(slot, sizeof(void*)) ? OriginalSlot(table, *index, _originalOf) : nullptr;
    if (!IsExecutableAddress(code))
        return Unbound(std::format("{}::[{}] does not hold code", entry.Class, *index));

    // Hook trampolines live outside the module and have no useful module offset.
    if (image->Contains(code))
        _slotAddresses.push_back(std::format("{}={}+{:#x}", key, entry.library, Rva(*image, code)));
    _record.vtables.emplace(key, ResolvedRecord::Slot{entry.library, Rva(*image, table), *index});
    return VirtualSlot{.Index = *index, .Table = table};
}

Result<int> GameDataResolver::FindOffset(const std::string& key)
{
    const auto& value = PlatformColumn(_file.offsets.at(key));
    if (!value)
        return Unbound(std::format("no {} offset", PlatformName));
    if (*value < 0)
        return Unbound(std::format("offset {} is negative", *value));

    _record.offsets.emplace(key, *value);
    return *value;
}

const ModuleImage* GameDataResolver::Image(const std::string& library)
{
    auto [it, added] = _images.try_emplace(library);
    if (added)
        FindModuleImage(library.c_str(), it->second);
    return it->second.Base ? &it->second : nullptr;
}

void* GameDataResolver::Table(const ModuleImage& image, const std::string& library, const std::string& className)
{
    auto [it, added] = _tables.try_emplace({library, className}, nullptr);
    if (added)
        it->second = FindVirtualTableIn(image, className.c_str());
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
