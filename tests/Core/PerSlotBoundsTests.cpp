#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <doctest/doctest.h>

using VoltMod::MaxPlayers;
using VoltMod::PerSlot;
using VoltMod::SlotEvents;

TEST_CASE("PerSlot valid slots round-trip independently")
{
    PerSlot<int> values;
    values[0] = 1;
    values[32] = 2;
    values[MaxPlayers - 1] = 3;

    CHECK(values[0] == 1);
    CHECK(values[32] == 2);
    CHECK(values[MaxPlayers - 1] == 3);
}

TEST_CASE("Reset on a negative slot is a no-op")
{
    PerSlot<int> values;
    values[0] = 7;

    values.Reset(-1);

    CHECK(values[0] == 7);
}

TEST_CASE("Reset at MaxPlayers is a no-op")
{
    PerSlot<int> values;
    values[MaxPlayers - 1] = 9;

    values.Reset(MaxPlayers);

    CHECK(values[MaxPlayers - 1] == 9);
}

TEST_CASE("BindReset ignores an invalid slot raised through SlotEvents")
{
    SlotEvents slots;
    PerSlot<int> values;
    values.BindReset(slots);
    values[0] = 5;

    // Neither call should touch a real slot's value or assert-fail on the invalid index.
    slots.Raise(-1);
    slots.Raise(MaxPlayers);

    CHECK(values[0] == 5);
}
