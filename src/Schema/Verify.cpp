#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Schema/Layout.hpp>
#include <format>
#include <schemasystem/schemasystem.h>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod::Schema
{

// Set once by BindSchemaVerification. A process-wide file-static is right here: the schema
// system is one engine object shared by every plugin, and the offsets it reports are constants
// of the loaded binary rather than per-load state.
static ISchemaSystem* g_schema = nullptr;

static std::string ServerModuleName()
{
#ifdef _WIN32
    return "server.dll";
#else
    return "libserver.so";
#endif
}

/**
 * Find @p field on @p klass or its bases, most-derived first.
 *
 * The offsets the schema reports are already flattened for single inheritance, so a base
 * class's offset is usable as-is on the derived object.
 */
static const SchemaClassFieldData_t* FindField(const CSchemaClassInfo* klass, std::string_view field)
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

void BindSchemaVerification(ISchemaSystem* system)
{
    g_schema = system;
}

Status VerifySchemaLayout()
{
    if (!g_schema || !g_schema->SchemaSystemIsReady())
        return std::unexpected(Error::NotReady("schema system not ready"));

    const std::string moduleName = ServerModuleName();
    CSchemaSystemTypeScope* server = g_schema->FindTypeScopeForModule(moduleName.c_str());
    CSchemaSystemTypeScope* global = g_schema->GlobalTypeScope();
    if (!server)
        return std::unexpected(Error::NotReady(std::format("no type scope for {} yet", moduleName)));

    // Every mismatch is collected rather than reported one at a time: after a CS2 update a
    // whole class usually shifts at once, and the first line alone would understate the work.
    std::vector<std::string> drift;
    for (const ClassLayout& expected : GeneratedLayout())
    {
        const std::string name(expected.Name);
        CSchemaClassInfo* live = server->FindDeclaredClass(name.c_str()).Get();
        if (!live && global)
            live = global->FindDeclaredClass(name.c_str()).Get();

        if (!live)
        {
            drift.push_back(std::format("{}: no longer in the schema", expected.Name));
            continue;
        }

        if (live->m_nSize != expected.Size)
            drift.push_back(std::format("{}: size {} -> {}", expected.Name, expected.Size, live->m_nSize));

        for (const FieldLayout& want : expected.Fields)
        {
            const SchemaClassFieldData_t* found = FindField(live, want.Name);
            if (!found)
            {
                drift.push_back(std::format("{}::{}: gone", expected.Name, want.Name));
                continue;
            }

            if (found->m_nSingleInheritanceOffset != want.Offset)
            {
                drift.push_back(std::format("{}::{}: offset {} -> {}", expected.Name, want.Name, want.Offset,
                                            found->m_nSingleInheritanceOffset));
                continue;
            }

            int size = 0;
            uint8_t alignment = 0;
            if (found->m_pType && found->m_pType->GetSizeAndAlignment(size, alignment) && size != want.Size)
            {
                drift.push_back(std::format("{}::{}: size {} -> {}", expected.Name, want.Name, want.Size, size));
            }
        }
    }

    if (drift.empty())
        return {};

    std::string message = "schema drift (regenerate with voltmod schemagen):";
    for (const std::string& line : drift)
        message += std::format("\n  {}", line);
    return std::unexpected(Error::Invalid(message));
}

}  // namespace VoltMod::Schema
