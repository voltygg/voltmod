#include "Dumper.hpp"

#include <VoltMod/Api.hpp>
#include <VoltMod/Core/Json.hpp>
#include <VoltMod/Unsafe/Api.hpp>
#include <cstdint>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <schemasystem/schemasystem.h>
#include <schemasystem/schematypes.h>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

using VoltMod::Error;
using VoltMod::Json;
using VoltMod::Result;

namespace SchemaDump
{

// The chain object every replicated sub-object hangs off; the generator turns a non-negative
// chain offset into a NotifyThroughChain setter.
static constexpr std::string_view ChainField = "__m_pChainEntity";

// ---- The JSON IR. Public aggregates, so Glaze reflects them with no registration. ----------

/**
 * A field's type, as the schema itself spells it.
 *
 * `atomic` is carried separately from `category` because every CUtlVector is SCHEMA_TYPE_ATOMIC:
 * the category alone cannot tell a collection from a plain atomic, and a collection hangs its
 * element type off a different member than a pointer or a fixed array does.
 */
struct TypeInfo
{
    std::string name;
    std::string category;
    std::optional<std::string> atomic;
    std::optional<std::string> inner;
    std::optional<int32_t> extent;
    std::optional<std::string> declared;
};

struct FieldInfo
{
    std::string name;
    int32_t offset = 0;
    int32_t size = 0;
    TypeInfo type;
};

struct BaseInfo
{
    std::string name;
    int32_t offset = 0;
};

struct ClassInfo
{
    int32_t size = 0;
    std::vector<BaseInfo> bases;
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

/** Ordered maps keep the emitted file stable across runs, so `git diff` is the drift report. */
struct SchemaDoc
{
    int format = 1;
    std::vector<std::string> scopes;
    std::map<std::string, ClassInfo> classes;
    std::map<std::string, EnumInfo> enums;
};

// ---- Schema walk ---------------------------------------------------------------------------

/** The framework's PlatformModuleName lives in its private Engine module, so spell it here. */
static std::string ServerModuleName()
{
#ifdef _WIN32
    return "server.dll";
#else
    return "libserver.so";
#endif
}

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

/** Describe @p type for the generator, naming the one inner type each category hangs elsewhere. */
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
        // Only the templated atomics carry an element type; a plain one has none.
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

/** The class's own `__m_pChainEntity`, walking single inheritance up until one turns up. */
static int32_t FindChainOffset(const CSchemaClassInfo* klass)
{
    for (; klass; klass = klass->m_nBaseClassCount > 0 ? klass->m_pBaseClasses[0].m_pClass : nullptr)
    {
        for (uint16_t i = 0; i < klass->m_nFieldCount; ++i)
        {
            const char* name = klass->m_pFields[i].m_pszName;
            if (name && ChainField == name)
                return klass->m_pFields[i].m_nSingleInheritanceOffset;
        }
    }
    return -1;
}

static ClassInfo DescribeClass(const CSchemaClassInfo* klass)
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

static EnumInfo DescribeEnum(const CSchemaEnumInfo* enumeration)
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

/**
 * Snapshot a CUtlTSHash's elements.
 *
 * The iteration API differs across SDK revisions; this is pinned to the conan hl2sdk headers
 * (Count / GetElements / Element). CUtlTSHash publishes no element typedef, so @p T names it.
 */
template <class T, class Hash>
static std::vector<T> HashElements(Hash& hash)
{
    const int count = hash.Count();
    if (count <= 0)
        return {};

    std::vector<UtlTSHashHandle_t> handles(static_cast<size_t>(count));
    const int written = hash.GetElements(0, count, handles.data());

    std::vector<T> out;
    out.reserve(static_cast<size_t>(written));
    for (int i = 0; i < written; ++i)
        out.push_back(hash.Element(handles[static_cast<size_t>(i)]));
    return out;
}

Result<DumpStats> WriteSchemaDump(VoltMod::Runtime& runtime, const std::filesystem::path& output)
{
    ISchemaSystem* schema = runtime.Unsafe.Interfaces.SchemaSystem;
    if (!schema)
        return std::unexpected(Error::NotReady("ISchemaSystem not available"));

    if (!schema->SchemaSystemIsReady())
        return std::unexpected(Error::NotReady("schema system not ready"));

    const std::string moduleName = ServerModuleName();
    CSchemaSystemTypeScope* scope = schema->FindTypeScopeForModule(moduleName.c_str());
    if (!scope)
        return std::unexpected(Error::NotReady(std::format("no type scope for {} yet", moduleName)));

    SchemaDoc doc;
    DumpStats stats;

    // The server scope alone is not self-contained: MoveType_t, HitGroup_t, CNetworkVarChainer
    // and the CPlayerPawnComponent base of the services classes all live in the global scope.
    // The server scope is walked last so a name it defines wins over the global one, and is
    // labelled "server" rather than the platform module name so a Linux dump stays identical.
    for (const auto& [name, source] :
         {std::pair<const char*, CSchemaSystemTypeScope*>{"global", schema->GlobalTypeScope()}, {"server", scope}})
    {
        if (!source)
            continue;

        doc.scopes.emplace_back(name);
        for (CSchemaClassInfo* klass : HashElements<CSchemaClassInfo*>(source->m_ClassBindings))
        {
            if (!klass || !klass->m_pszName)
                continue;

            ClassInfo info = DescribeClass(klass);
            stats.Fields += static_cast<int>(info.fields.size());
            if (!doc.classes.insert_or_assign(klass->m_pszName, std::move(info)).second)
                ++stats.Overrides;
        }

        for (CSchemaEnumInfo* enumeration : HashElements<CSchemaEnumInfo*>(source->m_EnumBindings))
        {
            if (!enumeration || !enumeration->m_pszName)
                continue;

            if (!doc.enums.insert_or_assign(enumeration->m_pszName, DescribeEnum(enumeration)).second)
                ++stats.Overrides;
        }
    }

    stats.Classes = static_cast<int>(doc.classes.size());
    stats.Enums = static_cast<int>(doc.enums.size());

    const std::string text = Json::WritePretty(doc);
    if (text.empty())
        return std::unexpected(Error::Invalid("failed to serialize the schema document"));

    std::error_code ec;
    std::filesystem::create_directories(output.parent_path(), ec);

    // Binary, so the committed baseline is byte-identical on Windows and Linux.
    std::ofstream file(output, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return std::unexpected(Error::Invalid(std::format("failed to open {}", output.string())));

    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.put('\n');
    if (!file)
        return std::unexpected(Error::Invalid(std::format("failed to write {}", output.string())));

    return stats;
}

}  // namespace SchemaDump
