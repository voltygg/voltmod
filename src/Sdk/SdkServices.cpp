#include "Sdk/Schema.hpp"

#include <CS2Kit/Core/ActiveService.hpp>
#include <CS2Kit/Sdk/SdkServices.hpp>

namespace CS2Kit::Sdk
{

SdkServices::SdkServices(Core::CoreServices& core)
    : InputHistory(core.Slots), Teleports(core.Slots), _schema(std::make_unique<SchemaService>())
{}

SdkServices::~SdkServices() = default;

void SetActiveSdkServices(SdkServices* services)
{
    Core::ActiveService<SdkServices>::Set(services);
}

SdkServices& Ctx()
{
    return Core::ActiveService<SdkServices>::Get();
}

SdkServices* CtxOrNull()
{
    return Core::ActiveService<SdkServices>::GetOrNull();
}

}  // namespace CS2Kit::Sdk
