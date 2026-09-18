#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace VoltMod
{

/** Addresses and offsets from a successful load, normalized for comparison across builds. */
struct ResolvedGameData
{
    struct Location
    {
        std::string Module;
        uint64_t Rva = 0;
    };

    struct Slot
    {
        std::string Module;
        uint64_t Table = 0;  ///< The class table's RVA.
        int Index = -1;
        uint64_t Code = 0;  ///< RVA of the function in that slot; 0 when a hook trampoline outside the module holds it.
    };

    std::string Build;
    std::map<std::string, Location> Functions;
    std::map<std::string, Location> Globals;
    std::map<std::string, Slot> VTables;
    std::map<std::string, int> Offsets;
};

/**
 * Write @p record to `addons/voltmod/gamedata/resolved.<platform>.json` once per server build.
 * Write failures are logged and do not fail plugin loading.
 */
void WriteResolvedGameData(const ResolvedGameData& resolved);

}  // namespace VoltMod
