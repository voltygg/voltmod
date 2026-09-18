#include "Core/Files/GameBuild.hpp"
#include "Engine/Memory/SigScanner.hpp"
#include "Host/Schema/SchemaDump.hpp"
#include "Host/Schema/SchemaFields.hpp"
#include "Host/Schema/SchemaService.hpp"
#include "Schema/Layout.hpp"

#include <VoltMod/Core/Files/Paths.hpp>
#include <VoltMod/Core/Log.hpp>
#include <entity2/entityclass.h>
#include <entity2/entitysystem.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <schemasystem/schemasystem.h>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod::Schema
{

/** Output consumed by `voltmod schemagen`. */
static constexpr std::string_view DumpPath = "addons/voltmod/schema/server.json";

/** Read the build stamp from @p path, or return empty. */
static std::string DumpedBuild(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return {};

    std::string head(256, '\0');
    file.read(head.data(), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<size_t>(file.gcount()));

    constexpr std::string_view key = "\"build\":";
    const size_t at = head.find(key);
    if (at == std::string::npos)
        return {};

    const size_t open = head.find('"', at + key.size());
    const size_t close = open == std::string::npos ? std::string::npos : head.find('"', open + 1);
    return close == std::string::npos ? std::string() : head.substr(open + 1, close - open - 1);
}

static CSchemaSystemTypeScope* ServerScope(ISchemaSystem* schema)
{
    if (!schema || !schema->SchemaSystemIsReady())
        return nullptr;
    const std::string moduleName = PlatformModuleName("server");
    return schema->FindTypeScopeForModule(moduleName.c_str());
}

void WriteSchemaDump(ISchemaSystem* schema, CGameEntitySystem* entities)
{
    static bool attempted = false;  // one try per process; the build stamp prevents repeats
    if (attempted || !entities)
        return;

    CSchemaSystemTypeScope* server = ServerScope(schema);
    const std::filesystem::path output = ResolvePath(DumpPath);
    if (!server || DumpedBuild(output) == GameBuild())
        return;

    // One serializer database covers every networked class.
    const CEntityClass* entityClass = entities->FindClassByName("CBaseEntity");
    const CNetworkSerializerClassInfo* serializer = entityClass ? entityClass->m_NetworkSerializerInfo : nullptr;
    if (!serializer || !serializer->m_pDatabase)
        return;

    attempted = true;
    const Status written =
        WriteDumpFile(schema->GlobalTypeScope(), server, *serializer->m_pDatabase, DumpPath, GameBuild());
    if (!written)
    {
        Log::Warn("Schema: no dump written to {}: {}", output.string(), written.error().Detail);
        return;
    }
    Log::Info("Schema: dumped game build {} to {}.", GameBuild(), output.string());
}

Status VerifySchemaLayout(ISchemaSystem* schema)
{
    CSchemaSystemTypeScope* server = ServerScope(schema);
    if (!server)
        return std::unexpected(Error::NotReady("the server schema scope is not ready"));
    CSchemaSystemTypeScope* global = schema->GlobalTypeScope();

    // Report all mismatches so one shifted class does not hide the rest.
    std::vector<std::string> drift;
    for (const FieldLayout& want : GeneratedLayout())
    {
        const std::string className(want.Class);
        CSchemaClassInfo* live = server->FindDeclaredClass(className.c_str()).Get();
        if (!live && global)
            live = global->FindDeclaredClass(className.c_str()).Get();

        const SchemaClassFieldData_t* found = live ? FindField(live, want.Field) : nullptr;
        if (!found)
        {
            drift.push_back(std::format("{}::{}: gone", want.Class, want.Field));
            continue;
        }

        if (found->m_nSingleInheritanceOffset != want.Offset)
        {
            drift.push_back(std::format("{}::{}: offset {} -> {}", want.Class, want.Field, want.Offset,
                                        found->m_nSingleInheritanceOffset));
            continue;
        }

        int size = 0;
        uint8_t alignment = 0;
        if (found->m_pType && found->m_pType->GetSizeAndAlignment(size, alignment) && size != want.Size)
            drift.push_back(std::format("{}::{}: size {} -> {}", want.Class, want.Field, want.Size, size));
    }

    if (drift.empty())
        return {};

    std::string message = std::format(
        "schema drift (accessors generated from game build {}, server is {}); "
        "load a plugin into a running map to write the dump, then regenerate "
        "with voltmod schemagen:",
        GeneratedFromBuild(), GameBuild());
    for (const std::string& line : drift)
        message += std::format("\n  {}", line);
    return std::unexpected(Error::Invalid(message));
}

}  // namespace VoltMod::Schema
