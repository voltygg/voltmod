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
        std::string library;
        uint64_t rva = 0;
    };

    struct Slot
    {
        std::string library;
        uint64_t table = 0;  ///< The class table's RVA.
        int index = -1;
    };

    std::string build;
    std::map<std::string, Location> functions;
    std::map<std::string, Location> globals;
    std::map<std::string, Slot> vtables;
    std::map<std::string, int> offsets;
};

/**
 * Write @p record to `addons/voltmod/gamedata/resolved.<platform>.json`, stamped with the running
 * server build. Skipped when the file already carries that build, so later plugins leave it alone;
 * a failed write is logged, never returned.
 */
void WriteResolvedRecord(ResolvedRecord record);

}  // namespace VoltMod
