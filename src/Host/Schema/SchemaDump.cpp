#include "Host/Schema/SchemaDump.hpp"

#include "Host/Schema/SchemaFields.hpp"

#include <VoltMod/Core/Files/File.hpp>
#include <VoltMod/Core/Text/EnumNames.hpp>
#include <VoltMod/Core/Text/Json.hpp>
#include <cstdint>
#include <map>
#include <optional>
#include <schemasystem/schemasystem.h>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod::Schema
{

// The document `voltmod framework schemagen` reads; member names are its JSON keys.

/** `inner` is a pointer, array or templated atomic's element type. */
struct TypeInfo
{
    std::string name;
    std::string category;
    std::optional<std::string> atomic;
    std::optional<std::string> inner;
    std::optional<int32_t> extent;
};

struct FieldInfo
{
    std::string name;
    int32_t offset = 0;
    int32_t size = 0;
    TypeInfo type;
    bool networked = false;
};

struct ClassInfo
{
    int32_t size = 0;
    std::string base;  // empty at the root; schema offsets flatten single inheritance
    int32_t chain_offset = -1;
    std::vector<FieldInfo> fields;
};

struct EnumItem
{
    std::string name;
    int64_t value = 0;
};

struct EnumInfo
{
    int32_t size = 0;
    std::vector<EnumItem> items;
};

/** Ordered maps keep the file stable. */
struct SchemaDoc
{
    std::string build;  // steam.inf ServerVersion
    std::map<std::string, ClassInfo> classes;
    std::map<std::string, EnumInfo> enums;
};

static std::string TypeName(CSchemaType* type)
{
    if (!type || !type->m_sTypeName.Get())
    {
        return {};
    }
    return type->m_sTypeName.Get();
}

static TypeInfo DescribeType(CSchemaType* type)
{
    if (!type)
    {
        return {};
    }

    TypeInfo out{.name = TypeName(type), .category = std::string(Name(type->m_eTypeCategory))};
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
        out.atomic = std::string(Name(type->m_eAtomicCategory));
        // Only templated atomics have an element type.
        if (type->m_eAtomicCategory == SCHEMA_ATOMIC_T || type->m_eAtomicCategory == SCHEMA_ATOMIC_COLLECTION_OF_T ||
            type->m_eAtomicCategory == SCHEMA_ATOMIC_TT)
        {
            out.inner = TypeName(static_cast<CSchemaType_Atomic_T*>(type)->m_pTemplateType);
        }
        break;
    default:
        break;
    }
    return out;
}

/** @p network is null for a class the engine does not network. */
static ClassInfo DescribeClass(const CSchemaClassInfo* klass, const CNetworkSerializerClassInfo* network)
{
    const CSchemaClassInfo* base = klass->m_nBaseClassCount > 0 ? klass->m_pBaseClasses[0].m_pClass : nullptr;
    // A non-negative offset enables a NotifyComponentOwner setter.
    const SchemaClassFieldData_t* chain = FindField(klass, ChainField);

    ClassInfo out{.size = klass->m_nSize,
                  .base = base && base->m_pszName ? base->m_pszName : "",
                  .chain_offset = chain ? chain->m_nSingleInheritanceOffset : -1};

    out.fields.reserve(klass->m_nFieldCount);
    for (uint16_t i = 0; i < klass->m_nFieldCount; ++i)
    {
        const auto& field = klass->m_pFields[i];
        if (!field.m_pszName)
        {
            continue;
        }

        int size = 0;
        uint8_t alignment = 0;
        if (field.m_pType)
        {
            field.m_pType->GetSizeAndAlignment(size, alignment);
        }

        out.fields.push_back({.name = field.m_pszName,
                              .offset = field.m_nSingleInheritanceOffset,
                              .size = size,
                              .type = DescribeType(field.m_pType),
                              .networked = network && network->FindField(field.m_pszName)});
    }

    return out;
}

static EnumInfo DescribeEnum(const CSchemaEnumInfo* enumeration)
{
    EnumInfo out{.size = enumeration->m_nSize};
    out.items.reserve(enumeration->m_nEnumeratorCount);

    for (uint16_t i = 0; i < enumeration->m_nEnumeratorCount; ++i)
    {
        const auto& item = enumeration->m_pEnumerators[i];
        if (item.m_pszName)
        {
            out.items.push_back({.name = item.m_pszName, .value = item.m_nValue});
        }
    }

    return out;
}

static const CNetworkSerializerClassInfo* NetworkClass(const CNetworkSerializerCodeGenDatabase& network,
                                                       const char* name)
{
    const auto index = network.m_ClassInfos.Find(name);
    return index == network.m_ClassInfos.InvalidIndex() ? nullptr : network.m_ClassInfos.Element(index);
}

/** Copy a CUtlTSHash's elements out. */
template <class T, class Hash>
static std::vector<T> HashElements(Hash& hash)
{
    const int count = hash.Count();
    if (count <= 0)
    {
        return {};
    }

    std::vector<UtlTSHashHandle_t> handles(static_cast<size_t>(count));
    const int written = hash.GetElements(0, count, handles.data());

    std::vector<T> out;
    out.reserve(static_cast<size_t>(written));
    for (int i = 0; i < written; ++i)
    {
        out.push_back(hash.Element(handles[static_cast<size_t>(i)]));
    }
    return out;
}

static void MergeScope(SchemaDoc& doc, CSchemaSystemTypeScope* scope, const CNetworkSerializerCodeGenDatabase& network)
{
    for (CSchemaClassInfo* klass : HashElements<CSchemaClassInfo*>(scope->m_ClassBindings))
    {
        if (klass && klass->m_pszName)
        {
            doc.classes.insert_or_assign(klass->m_pszName,
                                         DescribeClass(klass, NetworkClass(network, klass->m_pszName)));
        }
    }

    for (CSchemaEnumInfo* enumeration : HashElements<CSchemaEnumInfo*>(scope->m_EnumBindings))
    {
        if (enumeration && enumeration->m_pszName)
        {
            doc.enums.insert_or_assign(enumeration->m_pszName, DescribeEnum(enumeration));
        }
    }
}

Status WriteDumpFile(CSchemaSystemTypeScope* global, CSchemaSystemTypeScope* server,
                     const CNetworkSerializerCodeGenDatabase& network, std::string_view path,
                     std::string_view gameBuild)
{
    SchemaDoc doc{.build = std::string(gameBuild)};

    // Server definitions override global ones.
    if (global)
    {
        MergeScope(doc, global, network);
    }
    MergeScope(doc, server, network);

    const std::string text = Json::WritePretty(doc);
    if (text.empty())
    {
        return std::unexpected(Error::Invalid("failed to serialize the schema document"));
    }
    return WriteAllText(path, text + "\n");
}

}  // namespace VoltMod::Schema
