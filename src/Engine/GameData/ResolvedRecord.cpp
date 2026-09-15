#include "Engine/GameData/ResolvedRecord.hpp"

#include "Core/GameBuild.hpp"
#include "Engine/GameData/GameDataDocument.hpp"

#include <VoltMod/Core/File.hpp>
#include <VoltMod/Core/Json.hpp>
#include <VoltMod/Core/Log.hpp>
#include <format>

namespace VoltMod
{

void WriteResolvedRecord(ResolvedRecord record)
{
    const std::string path = std::format("addons/voltmod/gamedata/resolved.{}.json", PlatformName);
    record.build = std::string(GameBuild());
    if (const auto existing = Json::ReadFile<ResolvedRecord>(path); existing && existing->build == record.build)
        return;

    if (const Status written = WriteAllText(path, Json::WritePretty(record)); !written)
        Log::Warn("GameData: no record written to {}: {}", path, written.error().Detail);
    else
        Log::Info("GameData: recorded what resolved on server {} in {}.", record.build, path);
}

}  // namespace VoltMod
