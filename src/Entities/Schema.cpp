#include "Entities/Schema.hpp"

#include "Engine/SigScanner.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <cstring>
#include <schemasystem/schemasystem.h>
#include <string_view>

namespace VoltMod
{

Status SchemaService::Initialize()
{
    if (!_interfaces.SchemaSystem)
        return std::unexpected(Error::NotReady("ISchemaSystem not available"));

    Log::Info("Schema system initialized.");
    return {};
}

int SchemaService::GetOffset(const char* className, const char* fieldName, int expectedSize)
{
    auto* schemaSystem = _interfaces.SchemaSystem;
    if (!schemaSystem)
        return -1;

    auto classIt = _offsetCache.find(std::string_view(className));
    if (classIt != _offsetCache.end())
    {
        auto fieldIt = classIt->second.find(std::string_view(fieldName));
        if (fieldIt != classIt->second.end())
            return fieldIt->second;
    }

    const std::string moduleName = PlatformModuleName("server");

    CSchemaSystemTypeScope* pTypeScope = schemaSystem->FindTypeScopeForModule(moduleName.c_str());
    if (!pTypeScope)
    {
        Log::Error("Schema: Failed to find type scope for {}.", moduleName);
        return -1;
    }

    SchemaMetaInfoHandle_t<CSchemaClassInfo> hClassInfo = pTypeScope->FindDeclaredClass(className);
    CSchemaClassInfo* pClassInfo = hClassInfo.Get();
    if (!pClassInfo)
    {
        Log::Error("Schema: Class '{}' not found.", className);
        return -1;
    }

    for (int i = 0; i < pClassInfo->m_nFieldCount; ++i)
    {
        SchemaClassFieldData_t& field = pClassInfo->m_pFields[i];
        if (strcmp(field.m_pszName, fieldName) == 0)
        {
            if (expectedSize > 0 && field.m_pType)
            {
                int size = 0;
                uint8_t alignment = 0;
                if (field.m_pType->GetSizeAndAlignment(size, alignment) && size != expectedSize)
                    Log::Warn("Schema: {}::{} is {} bytes but the caller expects {} (schema drift?).", className,
                              fieldName, size, expectedSize);
            }

            int offset = field.m_nSingleInheritanceOffset;
            _offsetCache[className][fieldName] = offset;
            return offset;
        }
    }

    Log::Warn("Schema: Field '{}' not found in '{}'.", fieldName, className);
    return -1;
}

}  // namespace VoltMod
