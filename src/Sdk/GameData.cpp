#include "Sdk/SigScanner.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Paths.hpp>
#include <VoltMod/Core/StringUtils.hpp>
#include <VoltMod/Sdk/GameData.hpp>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace VoltMod::Sdk
{
using namespace VoltMod::Core;

bool GameData::Load(const std::string& path)
{
    try
    {
        auto fullPath = Core::ResolvePath(path);
        std::ifstream file(fullPath);
        if (!file.is_open())
        {
            Log::Warn("GameData file not found: {}", path);
            return false;
        }

        auto json = nlohmann::json::parse(file,
                                          /*cb=*/nullptr, /*allow_exceptions=*/true, /*ignore_comments=*/true);

#ifdef _WIN32
        constexpr const char* platform = "windows";
#else
        constexpr const char* platform = "linux";
#endif

        if (json.contains("offsets"))
        {
            for (auto& [name, entry] : json["offsets"].items())
            {
                if (entry.contains(platform))
                    _offsets[name] = entry[platform].get<int>();
            }
        }

        if (json.contains("signatures"))
        {
            for (auto& [name, entry] : json["signatures"].items())
            {
                if (!entry.contains(platform))
                    continue;

                auto& platEntry = entry[platform];
                SignatureEntry sig;
                sig.Library = entry.value("library", "server");
                sig.Pattern = platEntry.value("pattern", "");
                sig.Offset = platEntry.value("offset", 0);
                _signatures[name] = std::move(sig);
            }
        }

        Log::Info("GameData loaded: {} offsets, {} signatures.", _offsets.size(), _signatures.size());
        return true;
    }
    catch (const std::exception& e)
    {
        Log::Warn("Failed to parse GameData: {}", e.what());
        return false;
    }
}

int GameData::GetOffset(const std::string& name) const
{
    auto it = _offsets.find(name);
    return it != _offsets.end() ? it->second : -1;
}

void* GameData::FindSignature(const std::string& name) const
{
    if (auto it = _resolved.find(name); it != _resolved.end())
        return it->second.Match;

    auto it = _signatures.find(name);
    if (it == _signatures.end())
        return nullptr;

    auto& sig = it->second;
    return VoltMod::Sdk::FindPattern(sig.Library.c_str(), sig.Pattern);
}

void* GameData::ResolveSignature(const std::string& name) const
{
    if (auto it = _resolved.find(name); it != _resolved.end())
        return it->second.Resolved;

    void* match = FindSignature(name);
    if (!match)
        return nullptr;

    const int sigOffset = _signatures.at(name).Offset;  // entry exists - FindSignature matched
    if (sigOffset == 0)
        return match;

    // A non-zero offset means the signature points at a 4-byte little-endian rel32 displacement
    // located `sigOffset` bytes into the match; resolve it to the absolute target address.
    auto addr = ResolveRelativeAddress(reinterpret_cast<uintptr_t>(match) + sigOffset, 0, 4);
    if (addr == 0)
        return nullptr;
    return reinterpret_cast<void*>(addr);
}

void GameData::ResolveAll()
{
    _resolved.clear();
    for (const auto& [name, sig] : _signatures)
    {
        ResolvedEntry entry;
        if (sig.Pattern.empty())
        {
            entry.Error = "empty pattern";
        }
        else
        {
            auto scan = FindPatternEx(sig.Library.c_str(), sig.Pattern);
            entry.Match = scan.Address;
            entry.Unique = scan.Unique;
            if (!scan.Address)
            {
                entry.Error = "pattern not found";
            }
            else if (sig.Offset == 0)
            {
                entry.Resolved = scan.Address;
            }
            else
            {
                auto addr = ResolveRelativeAddress(reinterpret_cast<uintptr_t>(scan.Address) + sig.Offset, 0, 4);
                entry.Resolved = reinterpret_cast<void*>(addr);
                if (addr == 0)
                    entry.Error = "rel32 resolution failed";
            }
        }
        _resolved[name] = std::move(entry);
    }
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
        summary = std::format("{}/{} signatures failed: {}", failed.size(), _resolved.size(),
                              StringUtils::Join(failed, ", "));
    if (!ambiguous.empty())
    {
        if (!summary.empty())
            summary += "; ";
        summary += std::format("ambiguous: {}", StringUtils::Join(ambiguous, ", "));
    }
    return summary;
}

}  // namespace VoltMod::Sdk
