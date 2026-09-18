#include "Host/HostGameData.hpp"

#include "Host/PluginHost.hpp"
#include "Host/ResolvedRecord.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Text/Json.hpp>
#include <VoltMod/Core/Text/Strings.hpp>
#include <cstdint>
#include <format>
#include <utility>

namespace VoltMod
{

void HostGameData::Resolve(std::string_view path, const OriginalSlotLookup& originalOf)
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

    WriteResolvedRecord(_resolver->Record());
}

GameDataEntry HostGameData::Lookup(GameDataKind kinds, HostString name)
{
    const std::string_view key = Text(name);
    const ResolvedEntry* entry = _resolver ? _resolver->Find(key) : nullptr;
    if (!entry)
        return Unbound("not in gamedata");
    if (!entry->Reason.empty())
        return Unbound(entry->Reason);
    if ((static_cast<uint32_t>(kinds) & static_cast<uint32_t>(entry->Kind)) == 0)
        return Unbound(std::format("in '{}', which this member does not bind from", entry->Sections[0]));

    return {.Found = true, .Address = entry->Address, .Value = entry->Value};
}

GameDataEntry HostGameData::Unbound(std::string reason)
{
    _reason = std::move(reason);
    return {.Reason = Borrowed(_reason)};
}

}  // namespace VoltMod
