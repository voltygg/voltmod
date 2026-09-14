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

/** Copy a CUtlTSHash's elements out. */
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

static const CNetworkSerializerClassInfo* NetworkClass(const CNetworkSerializerCodeGenDatabase& network,
                                                       const char* name)
{
    const auto index = network.m_ClassInfos.Find(name);
    return index == network.m_ClassInfos.InvalidIndex() ? nullptr : network.m_ClassInfos.Element(index);
}

static void MergeScope(SchemaDoc& doc, DumpStats& stats, std::string_view name, CSchemaSystemTypeScope* scope,
                       const CNetworkSerializerCodeGenDatabase& network)
{
    doc.scopes.emplace_back(name);
    for (CSchemaClassInfo* klass : HashElements<CSchemaClassInfo*>(scope->m_ClassBindings))
    {
        if (!klass || !klass->m_pszName)
            continue;

        ClassInfo info = DescribeClass(klass, NetworkClass(network, klass->m_pszName));
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

/** Binary mode keeps the file byte-identical across platforms. */
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

Result<DumpStats> WriteDumpFile(CSchemaSystemTypeScope* global, CSchemaSystemTypeScope* server,
                                const CNetworkSerializerCodeGenDatabase& network, const std::filesystem::path& output,
                                std::string_view gameBuild)
{
    if (!server)
        return std::unexpected(Error::NotReady("no server type scope"));

    SchemaDoc doc{.build = std::string(gameBuild)};
    DumpStats stats;

    // Server definitions override global ones.
    if (global)
        MergeScope(doc, stats, "global", global, network);
    MergeScope(doc, stats, "server", server, network);

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
