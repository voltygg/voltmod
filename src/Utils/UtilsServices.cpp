#include <CS2Kit/Core/ActiveService.hpp>
#include <CS2Kit/Utils/UtilsServices.hpp>

namespace CS2Kit::Utils
{

void SetActiveUtilsServices(UtilsServices* services)
{
    Core::ActiveService<UtilsServices>::Set(services);
}

UtilsServices& Ctx()
{
    return Core::ActiveService<UtilsServices>::Get();
}

UtilsServices* CtxOrNull()
{
    return Core::ActiveService<UtilsServices>::GetOrNull();
}

}  // namespace CS2Kit::Utils
