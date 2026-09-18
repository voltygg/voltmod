#include "Host/Schema/SchemaService.hpp"

#include "Host/EngineInterfaces.hpp"
#include "Schema/Layout.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/Memory/MemoryAccess.hpp>
#include <interfaces/interfaces.h>
#include <schemasystem/schemasystem.h>
#include <string_view>

namespace VoltMod
{

static constexpr std::string_view EntitySystemOffset = "GameEntitySystem";

void SchemaService::Start(SourceMM::ISmmAPI* metamod, PluginHost& host, IHostGameData* gameData)
{
    if (metamod != nullptr)
    {
        auto fromEngine = EngineInterfaces(metamod);
        if (Status found = ResolveInterface(_schema, fromEngine, SCHEMASYSTEM_INTERFACE_VERSION); !found)
            Log::Error("Schema: {}", found.error().Detail);
        if (Status found = ResolveInterface(_resources, fromEngine, GAMERESOURCESERVICESERVER_INTERFACE_VERSION);
            !found)
            Log::Error("Schema: {}", found.error().Detail);
    }

    if (gameData != nullptr)
    {
        const GameDataLocation entry = gameData->Lookup(GameDataSection::Offset, EntitySystemOffset);
        _entitySystemOffset = entry.Found ? entry.Value : -1;
    }

    _host = &host;
    Check();
}

void SchemaService::OnServerStartup()
{
    // The first map is the next chance when the schema scope was not there at host load.
    if (!_schemaLoaded)
        Check();
    Schema::WriteSchemaDump(_schema, Entities());
}

void SchemaService::Check()
{
    const Status verified = Schema::VerifySchemaLayout(_schema);
    _schemaLoaded = verified || verified.error().Code != ErrorCode::NotReady;
    if (!verified)
        Log::Error("Schema: {}", verified.error().Detail);
    else
        Log::Info("Schema: the generated layout matches game build {}.", Schema::GeneratedFromBuild());

    _host->SetSchemaLayout(Schema::GeneratedLayoutStamp(), verified.has_value());
}

CGameEntitySystem* SchemaService::Entities() const
{
    if (_resources == nullptr || _entitySystemOffset < 0)
        return nullptr;
    return ReadAt<CGameEntitySystem*>(_resources, _entitySystemOffset);
}

}  // namespace VoltMod
