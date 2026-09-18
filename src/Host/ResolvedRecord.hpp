#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace VoltMod
{

/** Addresses and offsets from a successful load, normalized for comparison across builds. */
struct ResolvedRecord
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
void WriteResolvedRecord(const ResolvedRecord& record);

}  // namespace VoltMod
