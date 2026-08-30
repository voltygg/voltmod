#pragma once

#include <VoltMod/Api.hpp>
#include <filesystem>
#include <string>

namespace SchemaDump
{

/** What one dump wrote, for the reply line. */
struct DumpStats
{
    int Classes = 0;
    int Enums = 0;
    int Fields = 0;
    /** Names the server scope redefined over the global one. Reported so a real collision
     *  cannot hide behind the flat merge. */
    int Overrides = 0;
};

/**
 * Walk the server type scope and write the schema IR to @p output.
 *
 * The IR is the sole input to `voltmod schemagen`; its shape is documented in README.md and
 * versioned by the `format` key. Only what the generator reads is written - no timestamp,
 * alignment or project name - so a committed baseline changes only when the schema does.
 *
 * @return ErrorCode::NotReady before the server module has registered its type scope.
 */
VoltMod::Result<DumpStats> WriteSchemaDump(VoltMod::Runtime& runtime, const std::filesystem::path& output);

}  // namespace SchemaDump
