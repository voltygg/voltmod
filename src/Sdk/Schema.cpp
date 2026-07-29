#include "Sdk/Schema.hpp"

#include <CS2Kit/Core/Services.hpp>
#include <CS2Kit/Sdk/GameInterfaces.hpp>
#include <CS2Kit/Utils/Log.hpp>
#include <schemasystem/schemasystem.h>
#include <string_view>

using CS2Kit::Core::Engine;

namespace CS2Kit::Sdk
{

using namespace CS2Kit::Utils;

bool SchemaService::Initialize()
{
    if (!Engine().Interfaces.SchemaSystem)
    {
        Log::Warn("ISchemaSystem not available.");
        return false;
    }

    Log::Info("Schema system initialized.");
    return true;
}

int SchemaService::GetOffset(const char* className, const char* fieldName, int expectedSize)
{
    auto* schemaSystem = Engine().Interfaces.SchemaSystem;
    if (!schemaSystem)
        return -1;

    auto classIt = _offsetCache.find(std::string_view(className));
    if (classIt != _offsetCache.end())
    {
        auto fieldIt = classIt->second.find(std::string_view(fieldName));
        if (fieldIt != classIt->second.end())
            return fieldIt->second;
    }

#ifdef _WIN32
    const char* moduleName = "server.dll";
#else
    const char* moduleName = "libserver.so";
#endif

    CSchemaSystemTypeScope* pTypeScope = schemaSystem->FindTypeScopeForModule(moduleName);
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

}  // namespace CS2Kit::Sdk
