#include <CS2Kit/Core/ActiveService.hpp>
#include <CS2Kit/Core/CoreServices.hpp>

namespace CS2Kit::Core
{

void SetActiveCoreServices(CoreServices* services)
{
    ActiveService<CoreServices>::Set(services);
}

CoreServices& Ctx()
{
    return ActiveService<CoreServices>::Get();
}

CoreServices* CtxOrNull()
{
    return ActiveService<CoreServices>::GetOrNull();
}

}  // namespace CS2Kit::Core
