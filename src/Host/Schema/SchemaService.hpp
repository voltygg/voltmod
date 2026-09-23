#pragma once

#include "Host/Plugins/PluginHost.hpp"

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Host/IHostGameData.hpp>

namespace VoltMod
{

/**
 * @brief The process's one comparison of the baked schema offsets against the live game.
 *
 * Both the comparison and the dump `voltmod framework schemagen` reads used to run once per plugin. The
 * layout compared here is the host's own copy of the generated offsets; the answer is recorded on
 * @ref PluginHost and only covers a plugin carrying the same layout stamp.
 */
class SchemaService
{
public:
    /**
     * Compare the layout once and record the answer on @p host.
     *
     * @p metamod resolves the schema system and the resource service; @p gameData says where the
     * entity system sits inside that service, which is what the dump needs.
     */
    void Initialize(SourceMM::ISmmAPI* metamod, PluginHost& host, IHostGameData* gameData);

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
    bool _schemaLoaded = false;  ///< false while the schema scope did not exist to compare against
};

namespace Schema
{

/**
 * @brief Compare @ref GeneratedLayout with the live schema.
 *
 * The host does this once for the process. Stale baked offsets would read and write wrong
 * addresses, so a plugin built from the same layout refuses to load when this fails.
 * @return Every mismatch in one message, or ErrorCode::NotReady before the server scope exists.
 */
Status VerifySchemaLayout(ISchemaSystem* schema);

/**
 * @brief Write `addons/voltmod/schema/server.json` for `voltmod framework schemagen` unless it matches this build.
 *
 * Networked fields come from the engine's serializers, which exist only with @p entities; null
 * writes nothing, which is what happens before the first map.
 */
void WriteSchemaDump(ISchemaSystem* schema, CGameEntitySystem* entities);

}  // namespace Schema
}  // namespace VoltMod
