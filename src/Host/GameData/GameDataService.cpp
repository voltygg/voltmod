#include "Host/GameData/GameDataService.hpp"

#include "Host/GameData/ResolvedGameData.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Text/Json.hpp>
#include <VoltMod/Core/Text/Strings.hpp>
#include <cstdint>
#include <format>
#include <utility>

namespace VoltMod
{

void GameDataService::Resolve(std::string_view path, const OriginalSlotLookup& originalOf)
{
    auto file = Json::ReadFile<GameDataDocument, Json::StrictReadOptions>(path);
    if (!file)
    {
        Log::Error("GameData: {} was not read: {}", path, file.error().Detail);
        return;
    }

    _file = std::make_unique<GameDataDocument>(std::move(*file));
    _originalOf = originalOf;
    _resolver = std::make_unique<GameDataResolver>(*_file, _originalOf);
    _resolver->ResolveAll();
    _resolver->LogSummary(path);

    const std::vector<std::string>& failures = _resolver->Failures();
    if (!failures.empty())
    {
        Log::Error("GameData: {} entries did not bind: {}", failures.size(), Strings::Join(failures, "; "));
        return;
    }

    WriteResolvedGameData(_resolver->Resolved());
}

GameDataLocation GameDataService::Lookup(GameDataSection sections, std::string_view key)
{
    const ResolvedEntry* entry = _resolver ? _resolver->Find(key) : nullptr;
    if (!entry)
    {
        return NotBound("not in gamedata");
    }
    if (!entry->Reason.empty())
    {
        return NotBound(entry->Reason);
    }
    if ((static_cast<uint32_t>(sections) & static_cast<uint32_t>(entry->Kind)) == 0)
    {
        return NotBound(std::format("in '{}', which this member does not bind from", entry->Sections[0]));
    }

    return {.Found = true, .Address = entry->Address, .Value = entry->Value};
}

GameDataLocation GameDataService::NotBound(std::string reason)
{
    _reason = std::move(reason);
    return {.Reason = _reason};
}

}  // namespace VoltMod
