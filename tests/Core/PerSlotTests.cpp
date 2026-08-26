#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <doctest/doctest.h>
#include <memory>

using VoltMod::Core::PerSlot;
using VoltMod::Core::SlotEvents;

TEST_CASE("An unbound PerSlot keeps its values when a slot changes hands")
{
    SlotEvents slots;
    PerSlot<int> values;
    values[3] = 7;

    slots.Raise(3);

    CHECK(values[3] == 7);
}

TEST_CASE("BindReset clears the slot that changed hands and leaves the others alone")
{
    SlotEvents slots;
    PerSlot<int> values;
    values.BindReset(slots);

    values[3] = 7;
    values[4] = 11;

    slots.Raise(3);

    CHECK(values[3] == 0);
    CHECK(values[4] == 11);
}

TEST_CASE("BindReset is idempotent, so a slot change resets once")
{
    SlotEvents slots;
    PerSlot<int> values;
    values.BindReset(slots);
    values.BindReset(slots);

    values[2] = 5;
    slots.Raise(2);

    CHECK(values[2] == 0);
}

TEST_CASE("Destroying a PerSlot unsubscribes, so a later slot change is inert")
{
    SlotEvents slots;
    {
        auto values = std::make_unique<PerSlot<int>>();
        values->BindReset(slots);
        (*values)[5] = 42;
    }

    // Would dispatch into freed storage if the destructor had not released the subscription.
    slots.Raise(5);
}
