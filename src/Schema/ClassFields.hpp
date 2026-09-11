#pragma once

#include <schemasystem/schematypes.h>
#include <string_view>

namespace VoltMod::Schema
{

/**
 * Find @p field on @p klass or its bases, most-derived first.
 *
 * Schema offsets are flattened for single inheritance, so base offsets work on derived objects.
 * Only the first base is followed, which is the one the flattening applies to.
 */
inline const SchemaClassFieldData_t* FindField(const CSchemaClassInfo* klass, std::string_view field)
{
    for (; klass; klass = klass->m_nBaseClassCount > 0 ? klass->m_pBaseClasses[0].m_pClass : nullptr)
    {
        for (uint16_t i = 0; i < klass->m_nFieldCount; ++i)
        {
            const char* name = klass->m_pFields[i].m_pszName;
            if (name && field == name)
                return &klass->m_pFields[i];
        }
    }
    return nullptr;
}

}  // namespace VoltMod::Schema
