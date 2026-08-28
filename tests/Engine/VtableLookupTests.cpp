#include "Engine/SigScanner.hpp"
#include "Engine/VtableLookup.hpp"

#include <cstdint>
#include <doctest/doctest.h>

using VoltMod::FindVTableSlot;
using VoltMod::IsReadableAddress;

// Distinct bodies on purpose: the release build folds identical functions (/OPT:ICF), which
// would give these one address and make "found the wrong slot" indistinguishable from success.
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

TEST_CASE("A member holding a wild pointer is skipped, not walked")
{
    // Exactly the shape that crashed: the first word is 0xFFFFFFFFFFFFFFFF, which is neither null
    // nor a mapping, so anything that reads through it before validating it faults.
    struct Object
    {
        void* Garbage;
        void** Vtable;
    };

    void* table[] = {reinterpret_cast<void*>(&Slot0), reinterpret_cast<void*>(&Slot1)};
    Object object{.Garbage = reinterpret_cast<void*>(~uintptr_t{0}), .Vtable = table};

    // Reaching the real table in the second word proves the first was skipped rather than fatal.
    auto found = FindVTableSlot(&object, reinterpret_cast<const void*>(&Slot1));
    REQUIRE(found.has_value());
    CHECK(found->Index == 1);
    CHECK(found->BaseOffset == static_cast<int>(sizeof(void*)));
}

TEST_CASE("A function the object does not carry is not found")
{
    void* table[] = {reinterpret_cast<void*>(&Slot0)};
    struct Object
    {
        void** Vtable;
    } object{.Vtable = table};

    CHECK_FALSE(FindVTableSlot(&object, reinterpret_cast<const void*>(&Slot1)).has_value());
}

TEST_CASE("Nothing is dereferenced without an instance and a function")
{
    int local = 0;
    CHECK_FALSE(FindVTableSlot(nullptr, reinterpret_cast<const void*>(&Slot0)).has_value());
    CHECK_FALSE(FindVTableSlot(&local, nullptr).has_value());
}
