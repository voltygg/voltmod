#pragma once

#include "Host/PluginHost.hpp"

#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Host/IHostGameData.hpp>

namespace VoltMod
{

/**
 * @brief The process's one comparison of the baked schema offsets against the live game.
 *
 * Both the comparison and the dump `voltmod schemagen` reads used to run once per plugin. The
 * layout compared here is the host's own copy of the generated offsets; the answer is recorded on
 * @ref PluginHost and only covers a plugin carrying the same layout stamp.
 */
class HostSchema
{
public:
    /**
     * Compare the layout once and record the answer on @p host.
     *
     * @p metamod resolves the schema system and the resource service; @p gameData says where the
     * entity system sits inside that service, which is what the dump needs.
     */
    void Start(SourceMM::ISmmAPI* metamod, PluginHost& host, IHostGameData* gameData);

    /** Write the dump as soon as a map's entities exist. It writes at most once per process, and
     *  nothing at all while the dump on disk already matches this game build. */
    void OnServerStartup();

private:
    /** Compare the layout and record the answer. */
    void Check();

    /** The entity system the engine caches in the resource service, null before the first map. */
    CGameEntitySystem* Entities() const;

    PluginHost* _host = nullptr;
    ISchemaSystem* _schema = nullptr;
    IGameResourceService* _resources = nullptr;
    int _entitySystemOffset = -1;
    bool _scopeReady = false;  ///< false while the schema scope did not exist to compare against
};

}  // namespace VoltMod
