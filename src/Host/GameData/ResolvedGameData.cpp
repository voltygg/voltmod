#include "Host/GameData/ResolvedGameData.hpp"

#include "Core/Files/GameBuild.hpp"
#include "Host/GameData/GameDataDocument.hpp"

#include <VoltMod/Core/Files/File.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Text/Json.hpp>
#include <format>

template <>
struct glz::meta<VoltMod::ResolvedGameData::Location>
{
    using T = VoltMod::ResolvedGameData::Location;
    static constexpr auto value = glz::object("module", &T::Module, "rva", &T::Rva);
};

template <>
struct glz::meta<VoltMod::ResolvedGameData::Slot>
{
    using T = VoltMod::ResolvedGameData::Slot;
    static constexpr auto value =
        glz::object("module", &T::Module, "table", &T::Table, "index", &T::Index, "code", &T::Code);
};

template <>
struct glz::meta<VoltMod::ResolvedGameData>
{
    using T = VoltMod::ResolvedGameData;
    static constexpr auto value = glz::object("build", &T::Build, "functions", &T::Functions, "globals", &T::Globals,
                                              "vtables", &T::VTables, "offsets", &T::Offsets);
};

namespace VoltMod
{

void WriteResolvedGameData(const ResolvedGameData& resolved)
{
    const std::string path = std::format("addons/voltmod/gamedata/resolved.{}.json", PlatformName);
    const std::string build(GameBuild());
    if (const auto existing = Json::ReadFile<ResolvedGameData>(path); existing && existing->Build == build)
        return;

    ResolvedGameData stamped = resolved;
    stamped.Build = build;
    if (const Status written = WriteAllText(path, Json::WritePretty(stamped)); !written)
        Log::Warn("GameData: no record written to {}: {}", path, written.error().Detail);
    else
        Log::Info("GameData: recorded what resolved on server {} in {}.", build, path);
}

}  // namespace VoltMod
