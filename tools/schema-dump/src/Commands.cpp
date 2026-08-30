#include "App.hpp"
#include "Dumper.hpp"

#include <VoltMod/Api.hpp>
#include <filesystem>
#include <string>

// Every framework name lives in VoltMod. Name the few a file leans on here, in the .cpp -
// never a using-directive, and never in a header.
using VoltMod::Caller;
using VoltMod::Reply;
using VoltMod::Result;

// The command argument types are the one nested namespace worth an alias.
namespace Args = VoltMod::Args;

namespace SchemaDump
{

/** Where a dump lands when the caller names no path. */
static std::filesystem::path DefaultOutput()
{
    return VoltMod::ResolvePath(VoltMod::AddonFile("schema-dump", "schema/server.json"));
}

// Registered explicitly from Start, so every handler is handed what it needs instead of
// reaching for a global. Console-only on purpose: a dump is a multi-megabyte write and this
// plugin sets no Policy, so a chat surface would only ever deny.
void RegisterCommands(App& app)
{
    app.Runtime.Commands.Add("schema_dump")
        .Describe("Write the engine schema to JSON for voltmod schemagen.")
        .ConsoleOnly()
        .Run([&app](Caller c, Args::Opt<Args::Rest> path) -> Result<Reply> {
            const std::filesystem::path output =
                path.Value ? std::filesystem::path(path.Value->Value) : DefaultOutput();

            auto written = WriteSchemaDump(app.Runtime, output);
            if (!written)
                return c.Fail("cmd.dumpFailed", {{"reason", written.error().Detail}});

            return c.Ok("cmd.dumpWritten", {{"path", output.string()},
                                            {"classes", std::to_string(written->Classes)},
                                            {"enums", std::to_string(written->Enums)},
                                            {"fields", std::to_string(written->Fields)},
                             {"overrides", std::to_string(written->Overrides)}});
        });
}

}  // namespace SchemaDump
