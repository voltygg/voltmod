#pragma once

#include <VoltMod/Engine/ConVars.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace VoltMod
{

/** Map validation and level changes through IVEngineServer2. */
class Map
{
public:
    /** Both dependencies must outlive this service. */
    Map(Interfaces& interfaces, ConVars& conVars);

    /** Whether the engine can load a mounted, non-workshop map. */
    bool IsValid(std::string_view name) const;

    /** Validate and queue a non-workshop map change. */
    bool ChangeLevel(std::string_view name);

    /** Queue `host_workshop_map`. Returns false for zero or when queuing fails. */
    bool ChangeToWorkshop(uint64_t workshopId);

    /** Current map from StartupServer. Empty after a late load until the next map. */
    const std::string& Current() const { return _current; }

    /** Framework hook entry point. Plugins read @ref Current. */
    void SetCurrent(std::string name) { _current = std::move(name); }

private:
    Interfaces& _interfaces;
    ConVars& _conVars;
    std::string _current;
};

}  // namespace VoltMod
