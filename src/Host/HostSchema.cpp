#include "Host/HostSchema.hpp"

#include "Host/SchemaCheck.hpp"
#include "Schema/Layout.hpp"

#include <ISmmAPI.h>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/Memory/MemoryAccess.hpp>
#include <VoltMod/Host/HostTypes.hpp>
#include <interfaces/interfaces.h>
#include <schemasystem/schemasystem.h>
#include <string_view>

namespace VoltMod
{

static constexpr std::string_view EntitySystemOffset = "GameEntitySystem";

void HostSchema::Start(SourceMM::ISmmAPI* metamod, PluginHost& host, IHostGameData* gameData)
{
    if (metamod != nullptr)
    {
        _schema = static_cast<ISchemaSystem*>(
            metamod->VInterfaceMatch(metamod->GetEngineFactory(), SCHEMASYSTEM_INTERFACE_VERSION, 0));
        _resources = static_cast<IGameResourceService*>(
            metamod->VInterfaceMatch(metamod->GetEngineFactory(), GAMERESOURCESERVICESERVER_INTERFACE_VERSION, 0));
    }

    if (gameData != nullptr)
    {
        const GameDataEntry entry = gameData->Lookup(GameDataKind::Offset, Borrowed(EntitySystemOffset));
        _entitySystemOffset = entry.Found ? entry.Value : -1;
    }

    _host = &host;
    Check();
}

void HostSchema::OnServerStartup()
{
    // The first map is the next chance when the schema scope was not there at host load.
    if (!_scopeReady)
        Check();
    Schema::WriteSchemaDump(_schema, Entities());
}

void HostSchema::Check()
{
    const Status verified = Schema::VerifySchemaLayout(_schema);
    _scopeReady = verified || verified.error().Code != ErrorCode::NotReady;
    if (!verified)
        Log::Error("Schema: {}", verified.error().Detail);
    else
        Log::Info("Schema: the generated layout matches game build {}.", Schema::GeneratedFromBuild());

    _host->SetSchemaLayout(Schema::GeneratedLayoutStamp(), verified.has_value());
}

CGameEntitySystem* HostSchema::Entities() const
{
    if (_resources == nullptr || _entitySystemOffset < 0)
        return nullptr;
    return ReadAt<CGameEntitySystem*>(_resources, _entitySystemOffset);
}

}  // namespace VoltMod
