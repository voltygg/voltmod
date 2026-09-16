#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace VoltMod
{

/** What a fully bound load resolved, as module-relative addresses, kept to compare builds offline. */
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
 * Write @p record to `addons/voltmod/gamedata/resolved.<platform>.json`, stamped with the running
 * server build. Skipped when the file already carries that build, so later plugins leave it alone;
 * a failed write is logged, never returned.
 */
void WriteResolvedRecord(const ResolvedRecord& record);

}  // namespace VoltMod
