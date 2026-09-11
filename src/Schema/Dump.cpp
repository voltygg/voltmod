#include "Schema/Dump.hpp"

#include "Schema/DumpDocument.hpp"

#include <VoltMod/Core/Json.hpp>
#include <format>
#include <fstream>
#include <schemasystem/schemasystem.h>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace VoltMod::Schema
{

/** Snapshot a CUtlTSHash through the HL2SDK Count/GetElements/Element API. */
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

/** Add a scope's classes and enums, counting redefinitions. */
static void MergeScope(SchemaDoc& doc, DumpStats& stats, std::string_view name, CSchemaSystemTypeScope* scope)
{
    doc.scopes.emplace_back(name);
    for (CSchemaClassInfo* klass : HashElements<CSchemaClassInfo*>(scope->m_ClassBindings))
    {
        if (!klass || !klass->m_pszName)
            continue;

        ClassInfo info = DescribeClass(klass);
        stats.Fields += static_cast<int>(info.fields.size());
        if (!doc.classes.insert_or_assign(klass->m_pszName, std::move(info)).second)
            ++stats.Overrides;
    }

    for (CSchemaEnumInfo* enumeration : HashElements<CSchemaEnumInfo*>(scope->m_EnumBindings))
    {
        if (!enumeration || !enumeration->m_pszName)
            continue;

        if (!doc.enums.insert_or_assign(enumeration->m_pszName, DescribeEnum(enumeration)).second)
            ++stats.Overrides;
    }
}

/** Write @p text to @p output, creating its directory. Binary mode keeps output identical across platforms. */
static Status WriteFile(const std::filesystem::path& output, std::string_view text)
{
    std::error_code ec;
    std::filesystem::create_directories(output.parent_path(), ec);

    std::ofstream file(output, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return std::unexpected(Error::Invalid(std::format("failed to open {}", output.string())));

    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.put('\n');
    if (!file)
        return std::unexpected(Error::Invalid(std::format("failed to write {}", output.string())));
    return {};
}

Result<DumpStats> WriteSchemaDump(CSchemaSystemTypeScope* global, CSchemaSystemTypeScope* server,
                                  const std::filesystem::path& output, std::string_view gameBuild)
{
    if (!server)
        return std::unexpected(Error::NotReady("no server type scope"));

    SchemaDoc doc{.build = std::string(gameBuild)};
    DumpStats stats;

    // Merge global first because server types depend on it; server definitions override collisions.
    if (global)
        MergeScope(doc, stats, "global", global);
    MergeScope(doc, stats, "server", server);

    stats.Classes = static_cast<int>(doc.classes.size());
    stats.Enums = static_cast<int>(doc.enums.size());

    const std::string text = Json::WritePretty(doc);
    if (text.empty())
        return std::unexpected(Error::Invalid("failed to serialize the schema document"));

    if (Status written = WriteFile(output, text); !written)
        return std::unexpected(written.error());
    return stats;
}

}  // namespace VoltMod::Schema
