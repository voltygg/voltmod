#pragma once

#include "Engine/Memory/Image.hpp"
#include "Engine/Memory/VtableLookup.hpp"

#include <VoltMod/Core/Result.hpp>
#include <map>
#include <string>
#include <tuple>
#include <utility>

namespace VoltMod
{

/** Loaded modules, class vtables and base subobjects, each looked up once. */
class ModuleCache
{
public:
    /** NotFound naming the module when it is not loaded. */
    Result<const Image*> Module(const std::string& moduleName);

    /** @p className's primary vtable, or with @p baseName that base's own table inside it. */
    Result<void*> Table(const std::string& moduleName, const std::string& className, const std::string& baseName = {});

    Result<BaseSubobject> Base(const std::string& moduleName, const std::string& className,
                               const std::string& baseName);

private:
    std::map<std::string, Image> _modules;
    std::map<std::pair<std::string, std::string>, void*> _tables;
    std::map<std::tuple<std::string, std::string, std::string>, Result<BaseSubobject>> _bases;
};

}  // namespace VoltMod
