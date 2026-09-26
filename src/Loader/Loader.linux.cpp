#include "Loader/Loader.hpp"

#include <dlfcn.h>

namespace VoltMod
{

void* OpenModule(const std::filesystem::path& path)
{
    return dlopen(path.c_str(), RTLD_NOW);
}

void* FindExport(void* module, const char* name)
{
    return dlsym(module, name);
}

std::string LastError()
{
    const char* message = dlerror();
    return message != nullptr ? message : "no message";
}

std::filesystem::path OwnPath()
{
    // Not &CreateInterface: another module's export of that name can interpose it.
    static const char inThisModule = 0;
    Dl_info info{};
    dladdr(&inThisModule, &info);
    // The engine may load this by a relative path.
    return std::filesystem::absolute(info.dli_fname).lexically_normal();
}

InterfaceFactory FactoryAt(const void* address)
{
    Dl_info info{};
    if (dladdr(address, &info) == 0)
    {
        return nullptr;
    }
    void* module = dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD);
    if (module == nullptr)
    {
        return nullptr;
    }
    auto* factory = reinterpret_cast<InterfaceFactory>(dlsym(module, "CreateInterface"));
    dlclose(module);  // RTLD_NOLOAD still takes a reference
    return factory;
}

}  // namespace VoltMod
