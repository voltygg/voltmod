#include "Sdk/Schema.hpp"

#include <CS2Kit/Core/ActiveService.hpp>
#include <CS2Kit/Sdk/SdkServices.hpp>

namespace CS2Kit::Sdk
{

SdkServices::SdkServices(Core::Scheduler& scheduler, Core::SlotEvents& slots, Utils::Translations& translations)
    : Scheduler(scheduler),
      Translations(translations),
      InputHistory(slots),
      Teleports(slots),
      _schema(std::make_unique<SchemaService>())
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
