#include "Ui/UiWriteCache.hpp"

#include <VoltMod/Core/SlotEvents.hpp>
#include <doctest/doctest.h>

using VoltMod::SlotEvents;
using VoltMod::UiProperty;
using VoltMod::UiWriteCache;

TEST_CASE("The first write of a value is new, and repeating it is not")
{
    UiWriteCache cache;

    CHECK(cache.Update(0, UiProperty::Text, "vm_row0_label", "text", "Kick"));
    CHECK_FALSE(cache.Update(0, UiProperty::Text, "vm_row0_label", "text", "Kick"));
    CHECK(cache.Update(0, UiProperty::Text, "vm_row0_label", "text", "Ban"));
    CHECK_FALSE(cache.Update(0, UiProperty::Text, "vm_row0_label", "text", "Ban"));
}

TEST_CASE("Panels and variables are remembered apart")
{
    UiWriteCache cache;
    CHECK(cache.Update(0, UiProperty::Text, "vm_row0_label", "text", "Kick"));

    // Same value, different panel, and same panel, different variable: both are unwritten.
    CHECK(cache.Update(0, UiProperty::Text, "vm_row1_label", "text", "Kick"));
    CHECK(cache.Update(0, UiProperty::Text, "vm_row0_label", "other", "Kick"));
}

TEST_CASE("A panel and a variable that concatenate the same way stay distinct")
{
    UiWriteCache cache;
    CHECK(cache.Update(0, UiProperty::Text, "row", "0_label", "Kick"));
    CHECK(cache.Update(0, UiProperty::Text, "row0", "_label", "Kick"));
    CHECK(cache.Update(0, UiProperty::Text, "row0_", "label", "Kick"));
}

TEST_CASE("UiWriteCache: a class and a dialog variable with the same name do not shadow each other")
{
    UiWriteCache cache;
    CHECK(cache.Update(0, UiProperty::Text, "vm_row0", "Hidden", "1"));

    // Same panel, same name, other kind: unwritten even though the text value already cached is "1".
    CHECK(cache.Update(0, UiProperty::Class, "vm_row0", "Hidden", "1"));
    CHECK_FALSE(cache.Update(0, UiProperty::Class, "vm_row0", "Hidden", "1"));
    CHECK_FALSE(cache.Update(0, UiProperty::Text, "vm_row0", "Hidden", "1"));
}

TEST_CASE("Slots are remembered apart")
{
    UiWriteCache cache;
    CHECK(cache.Update(0, UiProperty::Text, "vm_title", "text", "Admin Panel"));
    CHECK(cache.Update(1, UiProperty::Text, "vm_title", "text", "Admin Panel"));
    CHECK_FALSE(cache.Update(0, UiProperty::Text, "vm_title", "text", "Admin Panel"));
}

TEST_CASE("Input capture is tracked per slot and only reports changes")
{
    UiWriteCache cache;

    CHECK(cache.UpdateCapture(0, true));
    CHECK_FALSE(cache.UpdateCapture(0, true));
    CHECK(cache.UpdateCapture(0, false));
    CHECK(cache.UpdateCapture(1, false));
}

TEST_CASE("An out-of-range slot is never worth writing")
{
    UiWriteCache cache;
    CHECK_FALSE(cache.Update(-1, UiProperty::Text, "vm_title", "text", "Admin Panel"));
    CHECK_FALSE(cache.Update(VoltMod::MaxPlayers, UiProperty::Text, "vm_title", "text", "Admin Panel"));
    CHECK_FALSE(cache.UpdateCapture(-1, true));
    CHECK_FALSE(cache.FirstFailure(-1));
}

TEST_CASE("Forget makes the next write go through again")
{
    UiWriteCache cache;
    CHECK(cache.Update(0, UiProperty::Text, "vm_title", "text", "Admin Panel"));
    CHECK(cache.UpdateCapture(0, true));

    cache.Forget(0);

    CHECK(cache.Update(0, UiProperty::Text, "vm_title", "text", "Admin Panel"));
    CHECK(cache.UpdateCapture(0, true));
}

TEST_CASE("Forget leaves other slots alone")
{
    UiWriteCache cache;
    CHECK(cache.Update(0, UiProperty::Text, "vm_title", "text", "Admin Panel"));
    CHECK(cache.Update(1, UiProperty::Text, "vm_title", "text", "Admin Panel"));

    cache.Forget(0);

    CHECK(cache.Update(0, UiProperty::Text, "vm_title", "text", "Admin Panel"));
    CHECK_FALSE(cache.Update(1, UiProperty::Text, "vm_title", "text", "Admin Panel"));
}

TEST_CASE("A repeating failure is only worth reporting once")
{
    UiWriteCache cache;

    CHECK(cache.FirstFailure(0));
    CHECK_FALSE(cache.FirstFailure(0));
    CHECK(cache.FirstFailure(1));
}

TEST_CASE("Forgetting a slot does not re-arm its failure report")
{
    UiWriteCache cache;
    CHECK(cache.FirstFailure(0));

    // Forget runs on every failed write, so re-arming here would restore the per-frame logging
    // it exists to prevent.
    cache.Forget(0);
    CHECK_FALSE(cache.FirstFailure(0));
}

TEST_CASE("A new entity has told nobody anything, failures included")
{
    UiWriteCache cache;
    CHECK(cache.Update(0, UiProperty::Text, "vm_title", "text", "Admin Panel"));
    CHECK(cache.FirstFailure(0));

    cache.ForgetAll();

    CHECK(cache.Update(0, UiProperty::Text, "vm_title", "text", "Admin Panel"));
    CHECK(cache.FirstFailure(0));
}

TEST_CASE("A slot changing hands forgets what its last occupant was told")
{
    SlotEvents slots;
    UiWriteCache cache;
    cache.Bind(slots);

    CHECK(cache.Update(3, UiProperty::Text, "vm_title", "text", "Admin Panel"));
    CHECK(cache.FirstFailure(3));
    CHECK(cache.Update(4, UiProperty::Text, "vm_title", "text", "Admin Panel"));

    slots.Raise(3);

    CHECK(cache.Update(3, UiProperty::Text, "vm_title", "text", "Admin Panel"));
    CHECK(cache.FirstFailure(3));
    CHECK_FALSE(cache.Update(4, UiProperty::Text, "vm_title", "text", "Admin Panel"));
}
