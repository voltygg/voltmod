#include "Engine/SigScanner.hpp"
#include "Engine/VtableLookup.hpp"

#include <cstdint>
#include <doctest/doctest.h>

using VoltMod::FindVTableSlot;
using VoltMod::IsReadableAddress;

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

TEST_CASE("A readable span is readable, and an impossible address is not")
{
    int local = 0;
    CHECK(IsReadableAddress(&local, sizeof(local)));
    CHECK_FALSE(IsReadableAddress(nullptr, sizeof(void*)));
    CHECK_FALSE(IsReadableAddress(&local, 0));

    // The value that actually crashed FindVTableSlot: not null, and not a mapping either.
    CHECK_FALSE(IsReadableAddress(reinterpret_cast<const void*>(~uintptr_t{0}), sizeof(void*)));
    CHECK_FALSE(IsReadableAddress(reinterpret_cast<const void*>(uintptr_t{8}), sizeof(void*)));
}

// The fixture matches the blind walk's object length and terminates each table like `.rdata`.
static constexpr int kScannedWords = 8;

/** Stand-in engine object large enough for the blind walk. */
struct FakeInstance
{
    void* Words[kScannedWords]{};
};

/** Stand-in vtable terminated by the null entry the walk expects. */
template <int Entries>
struct FakeVTable
{
    void* Slots[Entries + 1]{};
};

TEST_CASE("A member holding a wild pointer is skipped, not walked")
{
    // Match the crash: the first word is neither null nor a mapped address.
    FakeVTable<2> table{{reinterpret_cast<void*>(&Slot0), reinterpret_cast<void*>(&Slot1), nullptr}};
    FakeInstance object{};
    object.Words[0] = reinterpret_cast<void*>(~uintptr_t{0});
    object.Words[1] = table.Slots;

    // Finding the real table in the second word proves the first was skipped safely.
    auto found = FindVTableSlot(&object, reinterpret_cast<const void*>(&Slot1));
    REQUIRE(found.has_value());
    CHECK(found->Index == 1);
    CHECK(found->BaseOffset == static_cast<int>(sizeof(void*)));
}

TEST_CASE("A function the object does not carry is not found")
{
    FakeVTable<1> table{{reinterpret_cast<void*>(&Slot0), nullptr}};
    FakeInstance object{};
    object.Words[0] = table.Slots;

    CHECK_FALSE(FindVTableSlot(&object, reinterpret_cast<const void*>(&Slot1)).has_value());
}

TEST_CASE("Nothing is dereferenced without an instance and a function")
{
    int local = 0;
    CHECK_FALSE(FindVTableSlot(nullptr, reinterpret_cast<const void*>(&Slot0)).has_value());
    CHECK_FALSE(FindVTableSlot(&local, nullptr).has_value());
}
