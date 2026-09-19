#include "Ui/WriteCache.hpp"

#include <doctest/doctest.h>

using VoltMod::WriteCache;
using VoltMod::WriteKind;

TEST_CASE("The first write of a value is new, and repeating it is not")
{
    WriteCache cache;

    CHECK(cache.Changed(0, WriteKind::Text, "vm_row0_label", "text", "Kick"));
    CHECK_FALSE(cache.Changed(0, WriteKind::Text, "vm_row0_label", "text", "Kick"));
    CHECK(cache.Changed(0, WriteKind::Text, "vm_row0_label", "text", "Ban"));
    CHECK_FALSE(cache.Changed(0, WriteKind::Text, "vm_row0_label", "text", "Ban"));
}

TEST_CASE("Elements and variables are remembered apart, even when they concatenate alike")
{
    WriteCache cache;
    CHECK(cache.Changed(0, WriteKind::Text, "vm_row0_label", "text", "Kick"));

    CHECK(cache.Changed(0, WriteKind::Text, "vm_row1_label", "text", "Kick"));
    CHECK(cache.Changed(0, WriteKind::Text, "vm_row0_label", "other", "Kick"));

    // The two halves must not run together into one key.
    CHECK(cache.Changed(0, WriteKind::Text, "row", "0_label", "Kick"));
    CHECK(cache.Changed(0, WriteKind::Text, "row0", "_label", "Kick"));
    CHECK(cache.Changed(0, WriteKind::Text, "row0_", "label", "Kick"));
}

TEST_CASE("A class and a text with the same name do not shadow each other")
{
    WriteCache cache;
    CHECK(cache.Changed(0, WriteKind::Text, "vm_row0", "hidden", "1"));

    CHECK(cache.Changed(0, WriteKind::Class, "vm_row0", "hidden", "1"));
    CHECK_FALSE(cache.Changed(0, WriteKind::Class, "vm_row0", "hidden", "1"));
    CHECK_FALSE(cache.Changed(0, WriteKind::Text, "vm_row0", "hidden", "1"));
}

TEST_CASE("Slots are remembered apart")
{
    WriteCache cache;
    CHECK(cache.Changed(0, WriteKind::Text, "vm_title", "text", "Admin Panel"));
    CHECK(cache.Changed(1, WriteKind::Text, "vm_title", "text", "Admin Panel"));
    CHECK_FALSE(cache.Changed(0, WriteKind::Text, "vm_title", "text", "Admin Panel"));
}

TEST_CASE("The cursor is tracked per slot and only reports changes")
{
    WriteCache cache;

    CHECK(cache.CursorChanged(0, true));
    CHECK_FALSE(cache.CursorChanged(0, true));
    CHECK(cache.CursorChanged(0, false));
    CHECK(cache.CursorChanged(1, false));
}

TEST_CASE("An out-of-range slot is never worth writing")
{
    WriteCache cache;
    CHECK_FALSE(cache.Changed(-2, WriteKind::Text, "vm_title", "text", "Admin Panel"));
    CHECK_FALSE(cache.Changed(VoltMod::MaxPlayers, WriteKind::Text, "vm_title", "text", "Admin Panel"));
    CHECK_FALSE(cache.CursorChanged(-2, true));
    CHECK_FALSE(cache.IsFirstFailure(-2));
}

TEST_CASE("Writes for everyone are remembered apart from every slot")
{
    WriteCache cache;
    CHECK(cache.Changed(VoltMod::EveryoneSlot, WriteKind::Text, "vm_title", "text", "Admin Panel"));
    CHECK_FALSE(cache.Changed(VoltMod::EveryoneSlot, WriteKind::Text, "vm_title", "text", "Admin Panel"));

    CHECK(cache.Changed(0, WriteKind::Text, "vm_title", "text", "Admin Panel"));
    cache.Invalidate(0);
    CHECK_FALSE(cache.Changed(VoltMod::EveryoneSlot, WriteKind::Text, "vm_title", "text", "Admin Panel"));
    cache.Invalidate(VoltMod::EveryoneSlot);
    CHECK(cache.Changed(VoltMod::EveryoneSlot, WriteKind::Text, "vm_title", "text", "Admin Panel"));
}

TEST_CASE("Invalidate makes the next write go through again, and leaves other slots alone")
{
    WriteCache cache;
    CHECK(cache.Changed(0, WriteKind::Text, "vm_title", "text", "Admin Panel"));
    CHECK(cache.CursorChanged(0, true));
    CHECK(cache.Changed(1, WriteKind::Text, "vm_title", "text", "Admin Panel"));

    cache.Invalidate(0);

    CHECK(cache.Changed(0, WriteKind::Text, "vm_title", "text", "Admin Panel"));
    CHECK(cache.CursorChanged(0, true));
    CHECK_FALSE(cache.Changed(1, WriteKind::Text, "vm_title", "text", "Admin Panel"));
}

TEST_CASE("A repeating failure is only worth reporting once")
{
    WriteCache cache;

    CHECK(cache.IsFirstFailure(0));
    CHECK_FALSE(cache.IsFirstFailure(0));
    CHECK(cache.IsFirstFailure(1));
}

TEST_CASE("Invalidating a slot does not reset its failure report")
{
    WriteCache cache;
    CHECK(cache.IsFirstFailure(0));

    // Invalidate runs on every failed write; resetting the flag here would log every frame again.
    cache.Invalidate(0);
    CHECK_FALSE(cache.IsFirstFailure(0));
}

TEST_CASE("A new entity has been told nothing, failures included")
{
    WriteCache cache;
    CHECK(cache.Changed(0, WriteKind::Text, "vm_title", "text", "Admin Panel"));
    CHECK(cache.IsFirstFailure(0));

    cache.Clear();

    CHECK(cache.Changed(0, WriteKind::Text, "vm_title", "text", "Admin Panel"));
    CHECK(cache.IsFirstFailure(0));
}

TEST_CASE("A slot changing hands forgets what its last occupant was told")
{
    WriteCache cache;

    CHECK(cache.Changed(3, WriteKind::Text, "vm_title", "text", "Admin Panel"));
    CHECK(cache.IsFirstFailure(3));
    CHECK(cache.Changed(4, WriteKind::Text, "vm_title", "text", "Admin Panel"));

    cache.RemoveSlot(3);

    CHECK(cache.Changed(3, WriteKind::Text, "vm_title", "text", "Admin Panel"));
    CHECK(cache.IsFirstFailure(3));
    CHECK_FALSE(cache.Changed(4, WriteKind::Text, "vm_title", "text", "Admin Panel"));
}
