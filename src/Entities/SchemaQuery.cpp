#include "Engine/SigScanner.hpp"
#include "Entities/SchemaResolve.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Engine/MemoryAccess.hpp>
#include <entity2/entityclass.h>
#include <entity2/entityinstance.h>
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
 * Set once at load and never cleared. A process-wide file-static is right here: the schema system
 * is one engine object shared by every plugin in the process, and the offsets it answers with are
 * constants of the loaded binary rather than per-load state.
 */
static ISchemaSystem* g_schema = nullptr;

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
 * Find @p field on @p klass or its base classes, most-derived first. The offsets the schema reports
 * are already flattened for single inheritance, so a base class's offset is usable as-is on the
 * derived object.
 */
static const SchemaClassFieldData_t* FindField(const CSchemaClassInfo* klass, std::string_view field)
{
    if (!klass)
        return nullptr;

    for (uint16_t i = 0; i < klass->m_nFieldCount; ++i)
    {
        const char* name = klass->m_pFields[i].m_pszName;
        if (name && field == name)
            return &klass->m_pFields[i];
    }

    for (uint8_t i = 0; i < klass->m_nBaseClassCount; ++i)
    {
        if (const auto* found = FindField(klass->m_pBaseClasses[i].m_pClass, field))
            return found;
    }
    return nullptr;
}

static FieldQueryResult QuerySchema(std::string_view klass, std::string_view field)
{
    if (!g_schema)
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

    const SchemaClassFieldData_t* found = FindField(info, field);
    if (!found)
        return {.Available = true, .Found = false, .Ref = {}};

    int size = 0;
    uint8_t alignment = 0;
    if (found->m_pType)
        found->m_pType->GetSizeAndAlignment(size, alignment);

    return {.Available = true,
            .Found = true,
            .Ref = {.Offset = found->m_nSingleInheritanceOffset,
                    .Size = size,
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
    if (!entity || !ref)
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
