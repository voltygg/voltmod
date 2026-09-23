#include "Host/GameData/ModuleCache.hpp"

#include "Engine/Memory/SigScanner.hpp"

#include <format>

namespace VoltMod
{

static std::unexpected<Error> Unbound(std::string reason)
{
    return std::unexpected(Error::NotFound(std::move(reason)));
}

Result<const Image*> ModuleCache::Module(const std::string& moduleName)
{
    auto [it, added] = _modules.try_emplace(moduleName);
    if (added)
    {
        FindImage(moduleName, it->second);
    }
    if (!it->second.Base)
    {
        return Unbound(std::format("module '{}' is not loaded", moduleName));
    }
    return &it->second;
}

Result<void*> ModuleCache::Table(const std::string& moduleName, const std::string& className,
                                 const std::string& baseName)
{
    if (!baseName.empty())
    {
        const auto base = Base(moduleName, className, baseName);
        if (!base)
        {
            return std::unexpected(base.error());
        }
        if (!base->Table)
        {
            return Unbound(std::format("'{}' has no vtable of its own in '{}'", baseName, className));
        }
        return base->Table;
    }

    const auto module = Module(moduleName);
    if (!module)
    {
        return std::unexpected(module.error());
    }
    auto [it, added] = _tables.try_emplace({moduleName, className}, nullptr);
    if (added)
    {
        it->second = FindVirtualTableIn(**module, className);
    }
    if (!it->second)
    {
        return Unbound(std::format("no vtable for '{}' in '{}'", className, moduleName));
    }
    return it->second;
}

Result<BaseSubobject> ModuleCache::Base(const std::string& moduleName, const std::string& className,
                                        const std::string& baseName)
{
    const auto module = Module(moduleName);
    if (!module)
    {
        return std::unexpected(module.error());
    }
    auto key = std::tuple{moduleName, className, baseName};
    auto it = _bases.find(key);
    if (it == _bases.end())
    {
        it = _bases.emplace(std::move(key), FindBaseIn(**module, className, baseName)).first;
    }
    return it->second;
}

}  // namespace VoltMod
