#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <filesystem>
#include <networksystem/inetworkserializer.h>
#include <string_view>

namespace VoltMod::Schema
{

/** Counts from one dump, used for logging. */
struct DumpStats
{
    int Classes = 0;
    int Enums = 0;
    int Fields = 0;
    int Overrides = 0;  // server definitions that replaced global ones
};

/**
 * Write @p server merged over @p global to @p output as `voltmod schemagen` input, stamped with
 * @p gameBuild; @p network marks the fields the engine sends to clients.
 *
 * @return ErrorCode::NotReady when @p server is null.
 */
Result<DumpStats> WriteDumpFile(CSchemaSystemTypeScope* global, CSchemaSystemTypeScope* server,
                                const CNetworkSerializerCodeGenDatabase& network, const std::filesystem::path& output,
                                std::string_view gameBuild);

}  // namespace VoltMod::Schema
