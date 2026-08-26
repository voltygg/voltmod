#include "Engine/GameDataFile.hpp"
#include "Engine/SigScanner.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Strings.hpp>
#include <VoltMod/Engine/GameData.hpp>
#include <format>
#include <utility>
#include <vector>

namespace VoltMod
{

/** Scan one signature pattern and record the match. */
static GameData::Resolution ScanSignature(const SignatureEntry& entry, ScanResult& scan)
{
    GameData::Resolution out{.Section = GameData::Kind::Signature, .Library = entry.Library};
    scan = FindPatternEx(entry.Library.c_str(), entry.Pattern);
    out.Address = scan.Address;
    out.Unique = scan.Unique;
    if (!scan.Image.Base)
        out.Error = std::format("module '{}' is not loaded", entry.Library);
    else if (!scan.Address)
        out.Error = "pattern not found";
    return out;
}

/** Turn a signature match into the rel32 target it points at. */
static GameData::Resolution ResolveAddress(const AddressEntry& entry, const GameData::Resolution& signature,
                                           const ModuleImage& image)
{
    GameData::Resolution out{.Section = GameData::Kind::Address};
    out.Unique = signature.Unique;
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

Status GameData::Load(const std::string& path)
{
    // Everything, not just the resolutions: a reload that kept the previous build stamp or a
    // stale entry would report a file it is no longer running on.
    _resolved.clear();
    _game.clear();
    _verified.clear();

    auto file = GameDataFile::Load(path, HostPlatform);
    if (!file)
    {
        Log::Warn("GameData: {}", file.error().Detail);
        return std::unexpected(file.error());
    }

    _game = file->Build.Game;
    _verified = file->Build.Verified;

    // Signatures first: an address entry is resolved from its signature's match.
    std::map<std::string, ModuleImage> images;
    for (const auto& [key, entry] : file->Signatures)
    {
        ScanResult scan;
        _resolved.emplace(key, ScanSignature(entry, scan));
        images.emplace(key, std::move(scan.Image));
    }

    for (const auto& [key, entry] : file->Addresses)
        _resolved.emplace(key, ResolveAddress(entry, _resolved.at(entry.Signature), images.at(entry.Signature)));

    // A vtable index needs no scanning; the class vtable it is counted in is only located for the
    // entries a DVP hook binds to, which Bindings::Bind does from Class and Library.
    for (const auto& [key, entry] : file->VTables)
        _resolved.emplace(
            key,
            Resolution{.Section = Kind::VTable, .Index = entry.Index, .Class = entry.Class, .Library = entry.Library});

    for (const auto& [key, entry] : file->Offsets)
        _resolved.emplace(key, Resolution{.Section = Kind::Offset, .Index = entry.Value});

    Log::Info("GameData loaded from {} (verified {}): {} signatures, {} addresses, {} vtables, {} offsets.", path,
              _verified.empty() ? "?" : _verified, file->Signatures.size(), file->Addresses.size(),
              file->VTables.size(), file->Offsets.size());
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
    std::vector<std::string> ambiguous;
    for (const auto& [name, entry] : _resolved)
    {
        if (!entry.Error.empty())
            failed.push_back(name);
        else if (!entry.Unique)
            ambiguous.push_back(name);
    }

    if (failed.empty() && ambiguous.empty())
        return {};

    std::string summary;
    if (!failed.empty())
        summary = std::format("{}/{} entries failed: {}", failed.size(), _resolved.size(), Strings::Join(failed, ", "));
    if (!ambiguous.empty())
    {
        if (!summary.empty())
            summary += "; ";
        summary += std::format("ambiguous: {}", Strings::Join(ambiguous, ", "));
    }
    return summary;
}

}  // namespace VoltMod
