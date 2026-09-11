#include "Schema/ClassFields.hpp"
#include "Schema/DumpDocument.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace VoltMod::Schema
{

// A non-negative offset enables a NotifyThroughChain setter.
static constexpr std::string_view ChainField = "__m_pChainEntity";

static std::string_view CategoryName(SchemaTypeCategory_t category)
{
    switch (category)
    {
    case SCHEMA_TYPE_BUILTIN:
        return "builtin";
    case SCHEMA_TYPE_POINTER:
        return "pointer";
    case SCHEMA_TYPE_BITFIELD:
        return "bitfield";
    case SCHEMA_TYPE_FIXED_ARRAY:
        return "fixed_array";
    case SCHEMA_TYPE_ATOMIC:
        return "atomic";
    case SCHEMA_TYPE_DECLARED_CLASS:
        return "declared_class";
    case SCHEMA_TYPE_DECLARED_ENUM:
        return "declared_enum";
    default:
        return "invalid";
    }
}

static std::string_view AtomicName(SchemaAtomicCategory_t atomic)
{
    switch (atomic)
    {
    case SCHEMA_ATOMIC_PLAIN:
        return "plain";
    case SCHEMA_ATOMIC_T:
        return "t";
    case SCHEMA_ATOMIC_COLLECTION_OF_T:
        return "collection_of_t";
    case SCHEMA_ATOMIC_TT:
        return "tt";
    case SCHEMA_ATOMIC_I:
        return "i";
    default:
        return "invalid";
    }
}

static std::string TypeName(CSchemaType* type)
{
    if (!type || !type->m_sTypeName.Get())
        return {};
    return type->m_sTypeName.Get();
}

static TypeInfo DescribeType(CSchemaType* type)
{
    TypeInfo out;
    if (!type)
        return out;

    out.name = TypeName(type);
    out.category = CategoryName(type->m_eTypeCategory);

    switch (type->m_eTypeCategory)
    {
    case SCHEMA_TYPE_POINTER:
        out.inner = TypeName(static_cast<CSchemaType_Ptr*>(type)->m_pObjectType);
        break;

    case SCHEMA_TYPE_FIXED_ARRAY:
    {
        auto* array = static_cast<CSchemaType_FixedArray*>(type);
        out.inner = TypeName(array->m_pElementType);
        out.extent = array->m_nElementCount;
        break;
    }

    case SCHEMA_TYPE_ATOMIC:
    {
        out.atomic = std::string(AtomicName(type->m_eAtomicCategory));
        // Only templated atomics have an element type.
        const bool templated = type->m_eAtomicCategory == SCHEMA_ATOMIC_T ||
                               type->m_eAtomicCategory == SCHEMA_ATOMIC_COLLECTION_OF_T ||
                               type->m_eAtomicCategory == SCHEMA_ATOMIC_TT;
        if (templated)
            out.inner = TypeName(static_cast<CSchemaType_Atomic_T*>(type)->m_pTemplateType);
        break;
    }

    case SCHEMA_TYPE_DECLARED_CLASS:
        out.declared = "class";
        break;

    case SCHEMA_TYPE_DECLARED_ENUM:
        out.declared = "enum";
        break;

    default:
        break;
    }

    return out;
}

static int32_t FindChainOffset(const CSchemaClassInfo* klass)
{
    const SchemaClassFieldData_t* found = FindField(klass, ChainField);
    return found ? found->m_nSingleInheritanceOffset : -1;
}

ClassInfo DescribeClass(const CSchemaClassInfo* klass)
{
    ClassInfo out;
    out.size = klass->m_nSize;
    out.chain_offset = FindChainOffset(klass);

    for (uint8_t i = 0; i < klass->m_nBaseClassCount; ++i)
    {
        const auto& base = klass->m_pBaseClasses[i];
        if (base.m_pClass && base.m_pClass->m_pszName)
            out.bases.push_back({.name = base.m_pClass->m_pszName, .offset = static_cast<int32_t>(base.m_nOffset)});
    }

    out.fields.reserve(klass->m_nFieldCount);
    for (uint16_t i = 0; i < klass->m_nFieldCount; ++i)
    {
        const auto& field = klass->m_pFields[i];
        if (!field.m_pszName)
            continue;

        int size = 0;
        uint8_t alignment = 0;
        if (field.m_pType)
            field.m_pType->GetSizeAndAlignment(size, alignment);

        out.fields.push_back({.name = field.m_pszName,
                              .offset = field.m_nSingleInheritanceOffset,
                              .size = size,
                              .type = DescribeType(field.m_pType)});
    }

    return out;
}

EnumInfo DescribeEnum(const CSchemaEnumInfo* enumeration)
{
    EnumInfo out;
    out.size = enumeration->m_nSize;
    out.items.reserve(enumeration->m_nEnumeratorCount);

    for (uint16_t i = 0; i < enumeration->m_nEnumeratorCount; ++i)
    {
        const auto& item = enumeration->m_pEnumerators[i];
        if (item.m_pszName)
            out.items.push_back({.name = item.m_pszName, .value = item.m_nValue});
    }

    return out;
}

}  // namespace VoltMod::Schema
