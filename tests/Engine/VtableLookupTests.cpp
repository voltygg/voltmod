#include "Engine/Memory/SigScanner.hpp"
#include "Engine/Memory/VtableLookup.hpp"

#include <cstdint>
#include <doctest/doctest.h>

using VoltMod::FindSlotInTable;
using VoltMod::FindVirtualTableByTypeName;
using VoltMod::IsReadableAddress;
using VoltMod::ScanRange;

// Keep the bodies distinct: /OPT:ICF otherwise gives them one address.
static int gSink = 0;

static void Slot0()
{
    gSink += 1;
}

static void Slot1()
{
    gSink += 2;
}

static constexpr int kMaxSlots = 16;

TEST_CASE("A readable span is readable, and an impossible address is not")
{
    int local = 0;
    CHECK(IsReadableAddress(&local, sizeof(local)));
    CHECK_FALSE(IsReadableAddress(nullptr, sizeof(void*)));
    CHECK_FALSE(IsReadableAddress(&local, 0));

    // Neither null nor mapped.
    CHECK_FALSE(IsReadableAddress(reinterpret_cast<const void*>(~uintptr_t{0}), sizeof(void*)));
    CHECK_FALSE(IsReadableAddress(reinterpret_cast<const void*>(uintptr_t{8}), sizeof(void*)));
}

TEST_CASE("A function is found in the slot that holds it")
{
    void* table[] = {reinterpret_cast<void*>(&Slot0), reinterpret_cast<void*>(&Slot1), nullptr};

    const auto found = FindSlotInTable(table, reinterpret_cast<const void*>(&Slot1), {}, kMaxSlots);
    REQUIRE(found.has_value());
    CHECK(*found == 1);
}

TEST_CASE("A table ends at the first slot holding no code")
{
    void* table[] = {reinterpret_cast<void*>(&Slot0), nullptr, reinterpret_cast<void*>(&Slot1), nullptr};

    CHECK_FALSE(FindSlotInTable(table, reinterpret_cast<const void*>(&Slot1), {}, kMaxSlots).has_value());
}

TEST_CASE("Nothing is dereferenced without a table and a function")
{
    void* table[] = {reinterpret_cast<void*>(&Slot0), nullptr};
    CHECK_FALSE(FindSlotInTable(nullptr, reinterpret_cast<const void*>(&Slot0), {}, kMaxSlots).has_value());
    CHECK_FALSE(FindSlotInTable(table, nullptr, {}, kMaxSlots).has_value());
}

TEST_CASE("An Itanium vtable is found from its type name, past tables that are not the primary one")
{
    // "N3Foo" sits inside a nested name; only the second "3Foo" names the class.
    static const char names[] = "N3Foo\0"
                                "3Foo";
    uintptr_t words[14]{};
    words[0] = 1;  // the typeinfo's own vptr
    words[1] = reinterpret_cast<uintptr_t>(&names[6]);

    // A secondary table: nonzero offset-to-top.
    words[5] = static_cast<uintptr_t>(-16);
    words[6] = reinterpret_cast<uintptr_t>(&words[0]);
    words[7] = reinterpret_cast<uintptr_t>(&Slot0);

    // A zero word before a typeinfo reference, but data after it.
    words[9] = reinterpret_cast<uintptr_t>(&words[0]);
    words[10] = reinterpret_cast<uintptr_t>(names);

    // The primary table.
    words[12] = reinterpret_cast<uintptr_t>(&words[0]);
    words[13] = reinterpret_cast<uintptr_t>(&Slot1);

    const ScanRange ranges[] = {
        {.Base = reinterpret_cast<const uint8_t*>(names), .Size = sizeof(names)},
        {.Base = reinterpret_cast<const uint8_t*>(words), .Size = sizeof(words)},
    };
    CHECK(FindVirtualTableByTypeName(ranges, "Foo") == static_cast<void*>(&words[13]));
}

TEST_CASE("A type name inside a longer mangled name does not identify a class")
{
    static const char names[] = "N3Foo";
    uintptr_t words[5]{};
    words[0] = 1;
    words[1] = reinterpret_cast<uintptr_t>(&names[1]);
    words[3] = reinterpret_cast<uintptr_t>(&words[0]);
    words[4] = reinterpret_cast<uintptr_t>(&Slot0);

    const ScanRange ranges[] = {
        {.Base = reinterpret_cast<const uint8_t*>(names), .Size = sizeof(names)},
        {.Base = reinterpret_cast<const uint8_t*>(words), .Size = sizeof(words)},
    };
    CHECK(FindVirtualTableByTypeName(ranges, "Foo") == nullptr);
}
