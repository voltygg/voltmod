#include "Host/ResolvedRecord.hpp"

#include "Core/Files/GameBuild.hpp"
#include "Host/GameDataDocument.hpp"

#include <VoltMod/Core/Files/File.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Text/Json.hpp>
#include <format>

template <>
struct glz::meta<VoltMod::ResolvedRecord::Location>
{
    using T = VoltMod::ResolvedRecord::Location;
    static constexpr auto value = glz::object("module", &T::Module, "rva", &T::Rva);
};

template <>
struct glz::meta<VoltMod::ResolvedRecord::Slot>
{
    using T = VoltMod::ResolvedRecord::Slot;
    static constexpr auto value = glz::object("module", &T::Module, "table", &T::Table, "index", &T::Index);
};

template <>
struct glz::meta<VoltMod::ResolvedRecord>
{
    using T = VoltMod::ResolvedRecord;
    static constexpr auto value = glz::object("build", &T::Build, "functions", &T::Functions, "globals", &T::Globals,
                                              "vtables", &T::VTables, "offsets", &T::Offsets);
};

namespace VoltMod
{

void WriteResolvedRecord(const ResolvedRecord& record)
{
    const std::string path = std::format("addons/voltmod/gamedata/resolved.{}.json", PlatformName);
    const std::string build(GameBuild());
    if (const auto existing = Json::ReadFile<ResolvedRecord>(path); existing && existing->Build == build)
        return;

    ResolvedRecord stamped = record;
    stamped.Build = build;
    if (const Status written = WriteAllText(path, Json::WritePretty(stamped)); !written)
        Log::Warn("GameData: no record written to {}: {}", path, written.error().Detail);
    else
        Log::Info("GameData: recorded what resolved on server {} in {}.", build, path);
}

}  // namespace VoltMod
