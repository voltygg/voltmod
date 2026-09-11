#include "Engine/SigScanner.hpp"
#include "Schema/ClassFields.hpp"
#include "Schema/Dump.hpp"

#include <VoltMod/Core/File.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Paths.hpp>
#include <VoltMod/Core/Strings.hpp>
#include <VoltMod/Schema/Layout.hpp>
#include <filesystem>
#include <format>
#include <fstream>
#include <schemasystem/schemasystem.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace VoltMod::Schema
{

// One engine schema object is shared by every plugin; its offsets are process-wide constants.
static ISchemaSystem* g_schema = nullptr;

void BindSchemaVerification(ISchemaSystem* system)
{
    g_schema = system;
}

/** Output read by `voltmod schemagen`. */
static constexpr std::string_view DumpPath = "addons/voltmod/schema/server.json";

/** Read the game's build number from steam.inf beside the addons tree. */
static std::string_view GameBuild()
{
    static const std::string build = [] {
        constexpr std::string_view key = "ServerVersion=";
        auto text = ReadAllText("steam.inf");
        const size_t at = text ? text->find(key) : std::string::npos;
        if (at == std::string::npos)
            return std::string("unknown");

        const size_t start = at + key.size();
        return Strings::Trim(std::string_view(*text).substr(start, text->find_first_of("\r\n", start) - start));
    }();
    return build;
}

/** Build stamped on an existing dump, or empty. Reads the document's first key, not all of it. */
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

/**
 * Write the dump needed for regeneration while the live schema is available.
 *
 * Write once per process because all plugins share one schema object. Failure is non-fatal because
 * the addons tree may be read-only.
 */
static void WriteDump(CSchemaSystemTypeScope* global, CSchemaSystemTypeScope* server)
{
    static bool written = false;
    if (std::exchange(written, true))
        return;

    const std::filesystem::path output = ResolvePath(DumpPath);
    auto stats = WriteSchemaDump(global, server, output, GameBuild());
    if (!stats)
    {
        Log::Warn("Schema: no dump written to {}: {}", output.string(), stats.error().Detail);
        return;
    }
    Log::Info("Schema: dumped game build {} to {} ({} classes, {} enums, {} fields).", GameBuild(), output.string(),
              stats->Classes, stats->Enums, stats->Fields);
}

Status VerifySchemaLayout()
{
    if (!g_schema || !g_schema->SchemaSystemIsReady())
        return std::unexpected(Error::NotReady("schema system not ready"));

    const std::string moduleName = PlatformModuleName("server");
    CSchemaSystemTypeScope* server = g_schema->FindTypeScopeForModule(moduleName.c_str());
    CSchemaSystemTypeScope* global = g_schema->GlobalTypeScope();
    if (!server)
        return std::unexpected(Error::NotReady(std::format("no type scope for {} yet", moduleName)));

    // Collect all mismatches so one shifted class does not hide the remaining drift.
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

    // One invariant: the dump on disk describes the running build. A plugin loaded earlier may
    // already have written it, and a stale one would regenerate accessors for the previous build.
    if (DumpedBuild(ResolvePath(DumpPath)) != GameBuild())
        WriteDump(global, server);

    if (drift.empty())
        return {};

    std::string message = std::format("schema drift (accessors generated from game build {}, server is {}); "
                                      "regenerate with voltmod schemagen:",
                                      GeneratedFromBuild(), GameBuild());
    for (const std::string& line : drift)
        message += std::format("\n  {}", line);
    return std::unexpected(Error::Invalid(message));
}

}  // namespace VoltMod::Schema
