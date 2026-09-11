#include "Engine/GameData/GameDataFile.hpp"
#include "Engine/Memory/SigScanner.hpp"
#include "Engine/Memory/VtableLookup.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Strings.hpp>
#include <VoltMod/Engine/GameData/GameData.hpp>
#include <VoltMod/Engine/Memory/OriginalVfn.hpp>
#include <cstdint>
#include <format>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace VoltMod
{

static GameData::Resolution ScanSignature(const SignatureEntry& entry, ScanResult& scan)
{
    GameData::Resolution out{.Section = GameData::Kind::Signature, .Library = entry.Library};
    scan = FindPatternEx(entry.Library.c_str(), entry.Pattern);
    out.Address = scan.Address;
    if (!scan.Image.Base)
        out.Error = std::format("module '{}' is not loaded", entry.Library);
    else if (!scan.Address)
        out.Error = "pattern not found";
    // Ambiguous matches are errors because Bindings rejects them and dependent addresses cannot be trusted.
    else if (!scan.Unique)
        out.Error = "pattern matched more than once";
    return out;
}

static GameData::Resolution ResolveAddress(const AddressEntry& entry, const GameData::Resolution& signature,
                                           const ModuleImage& image)
{
    GameData::Resolution out{.Section = GameData::Kind::Address};
    if (!signature.Error.empty() || !signature.Address)
    {
        out.Error = std::format("signature '{}' did not resolve", entry.Signature);
        return out;
    }

    const uintptr_t target =
        ResolveRelativeAddress(image, reinterpret_cast<uintptr_t>(signature.Address), entry.Rel32At);
    if (target == 0)
    {
        out.Error = std::format("rel32 at +{} is outside the module image", entry.Rel32At);
        return out;
    }

    out.Address = reinterpret_cast<void*>(target);
    return out;
}

/** Return the original function in @p table's @p index, when a hook has replaced that slot. */
static void* OriginalSlot(void* table, int index, const OriginalVfn& originalOf)
{
    void** slots = static_cast<void**>(table);
    if (!originalOf)
        return slots[index];

    const void* original = originalOf(&slots[index]);
    return original ? const_cast<void*>(original) : slots[index];
}

/** What every vtable entry in one load shares: the entries resolved before them, and the tables found so far. */
struct VTablePass
{
    const GameData::ResolutionMap& Resolved;
    const OriginalVfn& Original;
    std::map<std::pair<std::string, std::string>, void*> Tables;  ///< keyed by library and class
};

/** Locate a class vtable once per library and class; several entries share one table. */
static void* VTableFor(const VTableEntry& entry, VTablePass& pass)
{
    auto [at, added] = pass.Tables.try_emplace({entry.Library, entry.Class}, nullptr);
    if (added)
        at->second = FindVirtualTable(entry.Library.c_str(), entry.Class.c_str());
    return at->second;
}

/** Resolve by signature to survive vtable index shifts, falling back to the configured index. */
static GameData::Resolution ResolveVTable(const std::string& key, const VTableEntry& entry, VTablePass& pass)
{
    GameData::Resolution out{
        .Section = GameData::Kind::VTable, .Index = entry.Index, .Class = entry.Class, .Library = entry.Library};

    out.Table = VTableFor(entry, pass);
    if (!out.Table)
    {
        if (!entry.Signature.empty())
            Log::Warn("GameData: {} keeps index {}; no vtable for {}.", key, entry.Index, entry.Class);
        return out;
    }

    if (!entry.Signature.empty())
    {
        const GameData::Resolution& signature = pass.Resolved.at(entry.Signature);
        std::string keeps;
        if (!signature.Error.empty() || !signature.Address)
        {
            keeps = std::format("signature '{}' did not resolve", entry.Signature);
        }
        else if (const auto found = FindSlotInTable(out.Table, signature.Address, pass.Original, MaxVtableIndex))
        {
            if (*found != entry.Index)
                Log::Warn("GameData: {} moved from index {} to {}; update gamedata.jsonc.", key, entry.Index, *found);
            out.Index = *found;
        }
        else
        {
            keeps = std::format("'{}' is in no slot of {}", entry.Signature, entry.Class);
        }

        if (!keeps.empty())
            Log::Warn("GameData: {} keeps index {}; {}.", key, entry.Index, keeps);
    }

    if (void* held = OriginalSlot(out.Table, out.Index, pass.Original); IsExecutableAddress(held))
        out.Address = held;
    return out;
}

/** Format resolved vtable addresses as `key=library+offset` for minidump comparison. */
static std::string VTableAddresses(const GameData::ResolutionMap& resolved)
{
    std::map<std::string, ModuleImage> images;
    std::vector<std::string> bound;
    for (const auto& [key, entry] : resolved)
    {
        if (entry.Section != GameData::Kind::VTable || !entry.Address)
            continue;

        auto [image, first] = images.try_emplace(entry.Library);
        if (first)
            FindModuleImage(entry.Library.c_str(), image->second);

        // Hook trampolines outside the module have no useful module-relative offset.
        if (!image->second.Contains(entry.Address))
            continue;

        const auto* at = static_cast<const uint8_t*>(entry.Address);
        bound.push_back(std::format("{}={}+{:#x}", key, entry.Library, at - image->second.Base));
    }
    return Strings::Join(bound, ", ");
}

Status GameData::Load(std::string_view path, const OriginalVfn& originalOf)
{
    // Clear the verification date as well as resolutions so reloads cannot retain stale state.
    _resolved.clear();
    _verified.clear();

    auto file = GameDataFile::Load(path, HostPlatform);
    if (!file)
    {
        Log::Warn("GameData: {}", file.error().Detail);
        return std::unexpected(file.error());
    }

    _verified = file->Build.Verified;

    std::map<std::string, ModuleImage> images;
    for (const auto& [key, entry] : file->Signatures)
    {
        ScanResult scan;
        _resolved.emplace(key, ScanSignature(entry, scan));
        images.emplace(key, std::move(scan.Image));
    }

    for (const auto& [key, entry] : file->Addresses)
        _resolved.emplace(key, ResolveAddress(entry, _resolved.at(entry.Signature), images.at(entry.Signature)));

    VTablePass vtables{.Resolved = _resolved, .Original = originalOf};
    for (const auto& [key, entry] : file->VTables)
        _resolved.emplace(key, ResolveVTable(key, entry, vtables));

    for (const auto& [key, entry] : file->Offsets)
        _resolved.emplace(key, Resolution{.Section = Kind::Offset, .Index = entry.Value});

    Log::Info("GameData loaded from {} (verified {}): {} signatures, {} addresses, {} vtables, {} offsets.", path,
              _verified.empty() ? "?" : _verified, file->Signatures.size(), file->Addresses.size(),
              file->VTables.size(), file->Offsets.size());

    if (const std::string bound = VTableAddresses(_resolved); !bound.empty())
        Log::Info("GameData: vtable slots hold {}.", bound);

    // Report these once because downstream bindings only see an absent key.
    if (!file->OtherPlatformOnly.empty())
        Log::Info("GameData: {} entries are located for the other platform only and unavailable here: {}.",
                  file->OtherPlatformOnly.size(), Strings::Join(file->OtherPlatformOnly, ", "));
    return {};
}

size_t GameData::CountOf(Kind kind) const
{
    size_t count = 0;
    for (const auto& [name, entry] : _resolved)
        count += static_cast<size_t>(entry.Section == kind);
    return count;
}

std::string GameData::FailureSummary() const
{
    std::vector<std::string> failed;
    for (const auto& [name, entry] : _resolved)
    {
        if (!entry.Error.empty())
            failed.push_back(name);
    }

    if (failed.empty())
        return {};

    return std::format("{}/{} entries failed: {}", failed.size(), _resolved.size(), Strings::Join(failed, ", "));
}

}  // namespace VoltMod
