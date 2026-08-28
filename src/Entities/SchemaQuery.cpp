#include "Engine/SigScanner.hpp"
#include "Entities/SchemaResolve.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Engine/MemoryAccess.hpp>
#include <cstring>
#include <entity2/entityclass.h>
#include <entity2/entityinstance.h>
#include <entity2/entitysystem.h>
#include <networksystem/inetworkserializer.h>
#include <schemasystem/schemasystem.h>
#include <string>
#include <string_view>

namespace VoltMod
{

// The chain object every replicated sub-object hangs off. Only the two fields the notification
// needs are named; the padding is the engine's layout, re-verify after a CS2 update.
struct NetworkVarChainer
{
    CEntityInstance* Entity;
    uint8_t Pad[24];
    ChangeAccessorFieldPathIndex_t PathIndex;
};
static_assert(offsetof(NetworkVarChainer, PathIndex) == 0x20);

static constexpr std::string_view ChainField = "__m_pChainEntity";

/**
 * Set once at load and never cleared. A process-wide sink is right here: the schema system is one
 * engine object shared by every plugin in the process, and the offsets it answers with are
 * constants of the loaded binary rather than per-load state.
 */
static ISchemaSystem* g_schema = nullptr;

/**
 * The network serializer database, shared by every entity class.
 *
 * Whether a field replicates is not in the schema - the shipped server binary carries no
 * `MNetworkEnable` metadata on its fields, so asking the schema always answers "no". The
 * serializer database is the authority, and it is reachable only once the engine has created the
 * entity system, which is why an unavailable database means "ask again later" rather than
 * "not networked": caching a false there would silently stop every write from replicating.
 */
static CNetworkSerializerCodeGenDatabase* SerializerDatabase()
{
    auto* system = ::GameEntitySystem();
    if (!system)
        return nullptr;

    // Any registered class reaches the same database; CBaseEntity is the one always present.
    CEntityClass* anchor = system->FindClassByName("CBaseEntity");
    if (!anchor || !anchor->m_NetworkSerializerInfo)
        return nullptr;
    return anchor->m_NetworkSerializerInfo->m_pDatabase;
}

static bool IsNetworked(CNetworkSerializerCodeGenDatabase& database, std::string_view className,
                        std::string_view fieldName)
{
    // The dict and the field lookup both take C strings; this runs once per field per process.
    const int index = database.m_ClassInfos.Find(std::string(className).c_str());
    if (index == database.m_ClassInfos.InvalidIndex())
        return false;

    CNetworkSerializerClassInfo* info = database.m_ClassInfos[index];
    return info != nullptr && info->FindField(std::string(fieldName).c_str()) != nullptr;
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

/**
 * Find @p field on @p klass or the classes it derives from, most-derived first, so a field the
 * schema declares on a base (m_angEyeAngles on CCSPlayerPawnBase) answers when asked for on the
 * derived class and the other way round. The offsets the schema reports are already flattened for
 * single inheritance, so a base class's offset is usable as-is on the derived object.
 */
struct FoundField
{
    const SchemaClassFieldData_t* Field = nullptr;
    /** The class that declares it - the name the serializer database is keyed by, which is not
     *  necessarily the class the caller asked about. */
    const CSchemaClassInfo* Owner = nullptr;
};

static FoundField FindField(const CSchemaClassInfo* klass, std::string_view field)
{
    if (!klass)
        return {};

    for (uint16_t i = 0; i < klass->m_nFieldCount; ++i)
    {
        const char* name = klass->m_pFields[i].m_pszName;
        if (name && field == name)
            return {&klass->m_pFields[i], klass};
    }

    for (uint8_t i = 0; i < klass->m_nBaseClassCount; ++i)
    {
        if (FoundField found = FindField(klass->m_pBaseClasses[i].m_pClass, field); found.Field)
            return found;
    }
    return {};
}

static FieldQueryResult QuerySchema(std::string_view klass, std::string_view field)
{
    if (!g_schema)
        return {};

    CNetworkSerializerCodeGenDatabase* database = SerializerDatabase();
    if (!database)
        return {};

    const std::string moduleName = PlatformModuleName("server");
    CSchemaSystemTypeScope* scope = g_schema->FindTypeScopeForModule(moduleName.c_str());
    if (!scope)
    {
        // The scope appears with the server module, so this is "not yet", not "no such field".
        return {};
    }

    // FindDeclaredClass wants a NUL-terminated name and the callers all pass literals, but the
    // seam takes a view, so materialize one rather than assume.
    const std::string className(klass);
    CSchemaClassInfo* info = scope->FindDeclaredClass(className.c_str()).Get();
    if (!info)
    {
        Log::Warn("Schema: class '{}' not found in {}.", klass, moduleName);
        return {.Available = true, .Found = false, .Ref = {}};
    }

    const FoundField found = FindField(info, field);
    if (!found.Field)
        return {.Available = true, .Found = false, .Ref = {}};

    int size = 0;
    uint8_t alignment = 0;
    if (found.Field->m_pType)
        found.Field->m_pType->GetSizeAndAlignment(size, alignment);

    return {.Available = true,
            .Found = true,
            .Ref = {.Offset = found.Field->m_nSingleInheritanceOffset,
                    .Size = size,
                    .Networked = IsNetworked(*database, found.Owner->m_pszName, found.Field->m_pszName),
                    .ChainOffset = FindChainOffset(info)}};
}

Status BindSchemaSystem(ISchemaSystem* system)
{
    if (!system)
        return std::unexpected(Error::NotReady("ISchemaSystem not available"));

    g_schema = system;
    SetFieldQuery(&QuerySchema);
    Log::Info("Schema system initialized.");
    return {};
}

void MarkChanged(CEntityInstance* entity, const FieldRef& ref)
{
    if (!entity || !ref || !ref.Networked)
        return;

    if (ref.ChainOffset >= 0)
    {
        // The field replicates through a chain object embedded in the entity: the engine wants the
        // chainer's path index alongside the offset, and the entity to notify is the one the
        // chainer points at (usually this entity, but not always).
        auto* chainer = MemberPtr<NetworkVarChainer>(entity, ref.ChainOffset);
        if (chainer->Entity)
        {
            chainer->Entity->NetworkStateChanged(
                NetworkStateChangedData(static_cast<uint32>(ref.Offset), -1, chainer->PathIndex));
        }
        return;
    }

    entity->NetworkStateChanged(NetworkStateChangedData(static_cast<uint32>(ref.Offset)));
}

}  // namespace VoltMod
