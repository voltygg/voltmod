#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <filesystem>
#include <string_view>

namespace VoltMod::Schema
{

/** Counts from one dump, used for logging. */
struct DumpStats
{
    int Classes = 0;
    int Enums = 0;
    int Fields = 0;
    /** Number of server definitions that replaced global definitions. */
    int Overrides = 0;
};

/**
 * Walk @p server (merged over @p global) and write the schema IR to @p output, stamped with
 * @p gameBuild.
 *
 * The IR is the sole input to `voltmod schemagen`, as documented in docs/sdk/gamedata.md. Only
 * generator inputs are written, so the baseline changes only when the schema changes.
 *
 * The caller resolves the scopes, so readiness is decided once by the verification stage.
 *
 * @return ErrorCode::NotReady when @p server is null.
 */
Result<DumpStats> WriteSchemaDump(CSchemaSystemTypeScope* global, CSchemaSystemTypeScope* server,
                                  const std::filesystem::path& output, std::string_view gameBuild);

}  // namespace VoltMod::Schema
