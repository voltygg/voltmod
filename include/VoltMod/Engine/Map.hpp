#pragma once

#include <VoltMod/Engine/ConVars.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <cstdint>
#include <string>

namespace VoltMod
{

/**
 * @brief Map validation and level changes.
 *
 * Deliberately holds no map list. The engine exposes no usable one (a server's `mapcycle.txt`
 * frequently does not exist), so which maps an operator offers is plugin configuration, not
 * framework state.
 *
 * Every call routes through IVEngineServer2, which the SDK declares, so none of this needs a
 * gamedata signature or vtable index.
 */
class Map
{
public:
    /** @p interfaces supplies IVEngineServer2; @p conVars runs the workshop console line.
     *  Both must outlive this service. */
    Map(Interfaces& interfaces, ConVars& conVars);

    /**
     * Whether the engine can load @p name: the map file exists and its version is one this
     * build understands. Workshop maps are not covered - they are addressed by id, and the
     * engine cannot answer for one that is not mounted yet.
     * @return false when @p name is empty or the engine is unavailable.
     */
    bool IsValid(const char* name) const;

    /**
     * Change to a non-workshop map. Validates first, so a typo replies instead of dropping the
     * server into a failed load. The engine finishes the change asynchronously; this returns as
     * soon as the request is queued.
     * @return false when @p name is invalid or the engine is unavailable.
     */
    bool ChangeLevel(const char* name);

    /**
     * Change to a workshop map by published-file id.
     *
     * Goes through the `host_workshop_map` console command rather than ChangeLevel, which
     * leaves the previous workshop addon mounted.
     * @return false when @p workshopId is 0.
     */
    bool ChangeToWorkshop(uint64_t workshopId);

private:
    Interfaces& _interfaces;
    ConVars& _conVars;
};

}  // namespace VoltMod
