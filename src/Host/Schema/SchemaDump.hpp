#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <networksystem/inetworkserializer.h>
#include <string_view>

namespace VoltMod::Schema
{

/**
 * Write @p server merged over @p global to @p path as `voltmod framework schemagen` input, stamped with
 * @p gameBuild; @p network marks the fields the engine sends to clients.
 */
Status WriteDumpFile(CSchemaSystemTypeScope* global, CSchemaSystemTypeScope* server,
                     const CNetworkSerializerCodeGenDatabase& network, std::string_view path,
                     std::string_view gameBuild);

}  // namespace VoltMod::Schema
