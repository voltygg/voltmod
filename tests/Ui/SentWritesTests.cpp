#include "Ui/SentWrites.hpp"

#include <VoltMod/Core/SlotEvents.hpp>
#include <doctest/doctest.h>

using VoltMod::SlotEvents;
using VoltMod::WriteKind;
using VoltMod::SentWrites;

TEST_CASE("The first write of a value is new, and repeating it is not")
{
    SentWrites cache;

    CHECK(cache.Changed(0, WriteKind::Variable, "vm_row0_label", "text", "Kick"));
    CHECK_FALSE(cache.Changed(0, WriteKind::Variable, "vm_row0_label", "text", "Kick"));
    CHECK(cache.Changed(0, WriteKind::Variable, "vm_row0_label", "text", "Ban"));
    CHECK_FALSE(cache.Changed(0, WriteKind::Variable, "vm_row0_label", "text", "Ban"));
}

TEST_CASE("Panels and variables are remembered apart")
{
    SentWrites cache;
    CHECK(cache.Changed(0, WriteKind::Variable, "vm_row0_label", "text", "Kick"));

    // Same value, different panel, and same panel, different variable: both are unwritten.
    CHECK(cache.Changed(0, WriteKind::Variable, "vm_row1_label", "text", "Kick"));
    CHECK(cache.Changed(0, WriteKind::Variable, "vm_row0_label", "other", "Kick"));
}

TEST_CASE("A panel and a variable that concatenate the same way stay distinct")
{
    SentWrites cache;
    CHECK(cache.Changed(0, WriteKind::Variable, "row", "0_label", "Kick"));
    CHECK(cache.Changed(0, WriteKind::Variable, "row0", "_label", "Kick"));
    CHECK(cache.Changed(0, WriteKind::Variable, "row0_", "label", "Kick"));
}

TEST_CASE("SentWrites: a class and a dialog variable with the same name do not shadow each other")
{
    SentWrites cache;
    CHECK(cache.Changed(0, WriteKind::Variable, "vm_row0", "Hidden", "1"));

    // Same panel, same name, other kind: unwritten even though the text value already cached is "1".
    CHECK(cache.Changed(0, WriteKind::Class, "vm_row0", "Hidden", "1"));
    CHECK_FALSE(cache.Changed(0, WriteKind::Class, "vm_row0", "Hidden", "1"));
    CHECK_FALSE(cache.Changed(0, WriteKind::Variable, "vm_row0", "Hidden", "1"));
}

TEST_CASE("Slots are remembered apart")
{
    SentWrites cache;
    CHECK(cache.Changed(0, WriteKind::Variable, "vm_title", "text", "Admin Panel"));
    CHECK(cache.Changed(1, WriteKind::Variable, "vm_title", "text", "Admin Panel"));
    CHECK_FALSE(cache.Changed(0, WriteKind::Variable, "vm_title", "text", "Admin Panel"));
}

TEST_CASE("Input capture is tracked per slot and only reports changes")
{
    SentWrites cache;

    CHECK(cache.CaptureChanged(0, true));
    CHECK_FALSE(cache.CaptureChanged(0, true));
    CHECK(cache.CaptureChanged(0, false));
    CHECK(cache.CaptureChanged(1, false));
}

TEST_CASE("An out-of-range slot is never worth writing")
{
    SentWrites cache;
    CHECK_FALSE(cache.Changed(-2, WriteKind::Variable, "vm_title", "text", "Admin Panel"));
    CHECK_FALSE(cache.Changed(VoltMod::MaxPlayers, WriteKind::Variable, "vm_title", "text", "Admin Panel"));
    CHECK_FALSE(cache.CaptureChanged(-2, true));
    CHECK_FALSE(cache.IsFirstFailure(-2));
}

TEST_CASE("A shared panel dedupes in its own bucket, apart from every slot")
{
    SentWrites cache;
    CHECK(cache.Changed(VoltMod::EveryoneSlot, WriteKind::Variable, "vm_title", "text", "Admin Panel"));
    CHECK_FALSE(cache.Changed(VoltMod::EveryoneSlot, WriteKind::Variable, "vm_title", "text", "Admin Panel"));

    // A slot has been told nothing by the shared writes, and forgetting one leaves the other.
    CHECK(cache.Changed(0, WriteKind::Variable, "vm_title", "text", "Admin Panel"));
    cache.Forget(0);
    CHECK_FALSE(cache.Changed(VoltMod::EveryoneSlot, WriteKind::Variable, "vm_title", "text", "Admin Panel"));
    cache.Forget(VoltMod::EveryoneSlot);
    CHECK(cache.Changed(VoltMod::EveryoneSlot, WriteKind::Variable, "vm_title", "text", "Admin Panel"));
}

TEST_CASE("Forget makes the next write go through again")
{
    SentWrites cache;
    CHECK(cache.Changed(0, WriteKind::Variable, "vm_title", "text", "Admin Panel"));
    CHECK(cache.CaptureChanged(0, true));

    cache.Forget(0);

    CHECK(cache.Changed(0, WriteKind::Variable, "vm_title", "text", "Admin Panel"));
    CHECK(cache.CaptureChanged(0, true));
}

TEST_CASE("Forget leaves other slots alone")
{
    SentWrites cache;
    CHECK(cache.Changed(0, WriteKind::Variable, "vm_title", "text", "Admin Panel"));
    CHECK(cache.Changed(1, WriteKind::Variable, "vm_title", "text", "Admin Panel"));

    cache.Forget(0);

    CHECK(cache.Changed(0, WriteKind::Variable, "vm_title", "text", "Admin Panel"));
    CHECK_FALSE(cache.Changed(1, WriteKind::Variable, "vm_title", "text", "Admin Panel"));
}

TEST_CASE("A repeating failure is only worth reporting once")
{
    SentWrites cache;

    CHECK(cache.IsFirstFailure(0));
    CHECK_FALSE(cache.IsFirstFailure(0));
    CHECK(cache.IsFirstFailure(1));
}

TEST_CASE("Forgetting a slot does not reset its failure report")
{
    SentWrites cache;
    CHECK(cache.IsFirstFailure(0));

    // Forget runs on every failed write, so resetting it here would restore the per-frame logging
    // it exists to prevent.
    cache.Forget(0);
    CHECK_FALSE(cache.IsFirstFailure(0));
}

TEST_CASE("A new entity has told nobody anything, failures included")
{
    SentWrites cache;
    CHECK(cache.Changed(0, WriteKind::Variable, "vm_title", "text", "Admin Panel"));
    CHECK(cache.IsFirstFailure(0));

    cache.ForgetAll();

    CHECK(cache.Changed(0, WriteKind::Variable, "vm_title", "text", "Admin Panel"));
    CHECK(cache.IsFirstFailure(0));
}

TEST_CASE("A slot changing hands forgets what its last occupant was told")
{
    SlotEvents slots;
    SentWrites cache;
    cache.BindReset(slots);

    CHECK(cache.Changed(3, WriteKind::Variable, "vm_title", "text", "Admin Panel"));
    CHECK(cache.IsFirstFailure(3));
    CHECK(cache.Changed(4, WriteKind::Variable, "vm_title", "text", "Admin Panel"));

    slots.Raise(3);

    CHECK(cache.Changed(3, WriteKind::Variable, "vm_title", "text", "Admin Panel"));
    CHECK(cache.IsFirstFailure(3));
    CHECK_FALSE(cache.Changed(4, WriteKind::Variable, "vm_title", "text", "Admin Panel"));
}
