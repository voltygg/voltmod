#include <VoltMod/Commands/CommandSpec.hpp>
#include <doctest/doctest.h>

using namespace VoltMod::Commands;

TEST_CASE("A context with no translation table replies with the key itself")
{
    CommandContext ctx;
    CHECK(ctx.Tr == nullptr);
    CHECK(ctx.CallerSlot() == -1);
    CHECK(ctx.Ok("cmd.done").Message == "cmd.done");
    CHECK(ctx.Fail("cmd.denied").Message == "cmd.denied");
}
