#include "Engine/Memory/SigScanner.hpp"
#include "Engine/Memory/VtableLookup.hpp"

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>

using VoltMod::ErrorCode;
using VoltMod::FindBaseOffsetByTypeInfo;
using VoltMod::FindSlotInTable;
using VoltMod::FindVirtualTableByTypeInfo;
using VoltMod::FindVirtualTableByTypeName;
using VoltMod::IsReadableAddress;
using VoltMod::ScanRange;
using VoltMod::TypeInfoKinds;

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

// Stand-ins for the cxxabi vtables a typeinfo's vptr points into; compared, never read.
static constexpr TypeInfoKinds FakeKinds{.SingleBase = 0x5100, .MultipleBases = 0x5200};
static constexpr uintptr_t NoBases = 0x5000;

static const char RootName[] = "4Root";
static const char MiddleName[] = "6Middle";
static const char FilterName[] = "6Filter";
static const char OtherName[] = "5Other";
static const char DerivedName[] = "7Derived";

/** `__base_class_type_info::__offset_flags`: the offset above eight flag bits, marked public. */
static uintptr_t OffsetFlags(intptr_t offset, bool isVirtual = false)
{
    return static_cast<uintptr_t>(offset * 256) | (isVirtual ? 1u : 0u) | 2u;
}

static uintptr_t Word(const void* address)
{
    return reinterpret_cast<uintptr_t>(address);
}

/** Itanium typeinfos for `Derived : Middle, Filter` with Middle at 0, Filter at 48, and `Filter : Root`. */
struct FakeHierarchy
{
    uintptr_t Root[3] = {NoBases, Word(RootName), 0};
    uintptr_t Other[3] = {FakeKinds.SingleBase, Word(OtherName), Word(Root)};
    uintptr_t Middle[3] = {NoBases, Word(MiddleName), 0};
    uintptr_t Filter[3] = {FakeKinds.SingleBase, Word(FilterName), Word(Root)};
    // Flags 1 keeps the {flags, count} word from reading as an aligned pointer.
    uintptr_t Derived[7] = {FakeKinds.MultipleBases, Word(DerivedName), (uintptr_t{2} << 32) | 1, Word(Middle),
                            OffsetFlags(0),          Word(Filter),      OffsetFlags(48)};

    FakeHierarchy() = default;
    FakeHierarchy(const FakeHierarchy&) = delete;
    FakeHierarchy& operator=(const FakeHierarchy&) = delete;
};

TEST_CASE("An Itanium base offset adds up along the path to the base")
{
    const FakeHierarchy fake;

    CHECK(FindBaseOffsetByTypeInfo(fake.Derived, "Middle", FakeKinds).value_or(-1) == 0);
    CHECK(FindBaseOffsetByTypeInfo(fake.Derived, "Filter", FakeKinds).value_or(-1) == 48);
    CHECK(FindBaseOffsetByTypeInfo(fake.Derived, "Root", FakeKinds).value_or(-1) == 48);

    const auto missing = FindBaseOffsetByTypeInfo(fake.Derived, "Missing", FakeKinds);
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().Code == ErrorCode::NotFound);
}

TEST_CASE("Itanium typeinfos are told apart by their shape when the cxxabi vtables are unknown")
{
    const FakeHierarchy fake;

    CHECK(FindBaseOffsetByTypeInfo(fake.Derived, "Filter", {}).value_or(-1) == 48);
    CHECK(FindBaseOffsetByTypeInfo(fake.Derived, "Root", {}).value_or(-1) == 48);
}

TEST_CASE("A virtual or repeated Itanium base is refused")
{
    FakeHierarchy virtualBase;
    virtualBase.Derived[6] = OffsetFlags(-24, true);
    const auto isVirtual = FindBaseOffsetByTypeInfo(virtualBase.Derived, "Filter", FakeKinds);
    REQUIRE_FALSE(isVirtual.has_value());
    CHECK(isVirtual.error().Code == ErrorCode::Unsupported);

    // Other and Filter each lead to a Root.
    FakeHierarchy repeated;
    repeated.Derived[3] = Word(repeated.Other);
    const auto twice = FindBaseOffsetByTypeInfo(repeated.Derived, "Root", FakeKinds);
    REQUIRE_FALSE(twice.has_value());
    CHECK(twice.error().Code == ErrorCode::Invalid);
}

TEST_CASE("A secondary Itanium vtable is found by its typeinfo and offset-to-top")
{
    const FakeHierarchy fake;
    const uintptr_t typeInfo = Word(fake.Derived);

    uintptr_t words[12]{};
    words[2] = typeInfo;  // primary: offset-to-top 0 in words[1]
    words[3] = Word(&Slot0);
    words[5] = static_cast<uintptr_t>(-48);
    words[6] = typeInfo;
    words[7] = Word(&Slot1);

    const ScanRange ranges[] = {{.Base = reinterpret_cast<const uint8_t*>(words), .Size = sizeof(words)}};
    CHECK(FindVirtualTableByTypeInfo(ranges, fake.Derived, -48) == static_cast<void*>(&words[7]));
    CHECK(FindVirtualTableByTypeInfo(ranges, fake.Derived, 0) == static_cast<void*>(&words[3]));
    CHECK(FindVirtualTableByTypeInfo(ranges, fake.Derived, -16) == nullptr);

    // A second candidate means neither can be trusted.
    words[9] = static_cast<uintptr_t>(-48);
    words[10] = typeInfo;
    words[11] = Word(&Slot0);
    CHECK(FindVirtualTableByTypeInfo(ranges, fake.Derived, -48) == nullptr);
}

#ifdef _WIN32

using VoltMod::FindBaseInRtti;
using VoltMod::FindVirtualTableInRtti;
using VoltMod::PeRtti;

/**
 * A fake MSVC module of 0x400 bytes: type descriptors in "data" below 0x100, and the locators,
 * class hierarchy and vtables of `Derived : Middle (struct, at 0), Filter (at 8)` in "rdata" above.
 */
class FakeMsvcModule
{
public:
    FakeMsvcModule()
    {
        PutName(0x20, ".?AVDerived@@");
        PutName(0x70, ".?AVFilter@@");
        PutName(0xB0, ".?AUMiddle@@");

        PutLocator(0x100, 0);
        PutLocator(0x120, 8);

        Put32(0x208, 3);
        Put32(0x20C, 0x210);
        Put32(0x210, 0x220);
        Put32(0x214, 0x240);
        Put32(0x218, 0x260);
        PutBase(0x220, 0x10, 0);
        PutBase(0x240, 0xA0, 0);
        PutBase(0x260, 0x60, 8);

        PutWord(0x300, Word(At(0x100)));
        PutWord(0x308, Word(&Slot0));
        PutWord(0x310, Word(At(0x120)));
        PutWord(0x318, Word(&Slot1));
    }

    PeRtti Rtti() const
    {
        const auto* base = reinterpret_cast<const uint8_t*>(_words);
        return {.Base = base, .Size = 0x400, .Data = {base, 0x100}, .ReadOnlyData = {base + 0x100, 0x300}};
    }

    void* At(size_t rva) { return reinterpret_cast<uint8_t*>(_words) + rva; }

    void Put32(size_t rva, int32_t value) { std::memcpy(At(rva), &value, sizeof(value)); }

    void PutBase(size_t rva, int32_t typeDescriptor, int32_t offset, int32_t virtualOffset = -1)
    {
        Put32(rva, typeDescriptor);
        Put32(rva + 8, offset);
        Put32(rva + 12, virtualOffset);
    }

private:
    void PutWord(size_t rva, uintptr_t value) { std::memcpy(At(rva), &value, sizeof(value)); }
    void PutName(size_t rva, const char* name) { std::memcpy(At(rva), name, std::strlen(name) + 1); }

    void PutLocator(size_t rva, int32_t offset)
    {
        Put32(rva, 1);
        Put32(rva + 4, offset);
        Put32(rva + 12, 0x10);
        Put32(rva + 16, 0x200);
        Put32(rva + 20, static_cast<int32_t>(rva));
    }

    uint64_t _words[0x80]{};
};

TEST_CASE("An MSVC primary vtable follows the locator whose own RVA checks out")
{
    FakeMsvcModule module;
    CHECK(FindVirtualTableInRtti(module.Rtti(), "Derived") == module.At(0x308));
    CHECK(FindVirtualTableInRtti(module.Rtti(), "Missing") == nullptr);

    module.Put32(0x100 + 20, 0);
    CHECK(FindVirtualTableInRtti(module.Rtti(), "Derived") == nullptr);
}

TEST_CASE("An MSVC base gives its offset and the table of the locator at that offset")
{
    FakeMsvcModule module;

    const auto filter = FindBaseInRtti(module.Rtti(), "Derived", "Filter");
    REQUIRE(filter.has_value());
    CHECK(filter->Offset == 8);
    CHECK(filter->Table == module.At(0x318));

    const auto middle = FindBaseInRtti(module.Rtti(), "Derived", "Middle");
    REQUIRE(middle.has_value());
    CHECK(middle->Offset == 0);
    CHECK(middle->Table == module.At(0x308));

    CHECK(FindBaseInRtti(module.Rtti(), "Derived", "Missing").error().Code == ErrorCode::NotFound);
    CHECK(FindBaseInRtti(module.Rtti(), "Missing", "Filter").error().Code == ErrorCode::NotFound);
}

TEST_CASE("An MSVC base without its own locator still has an offset")
{
    FakeMsvcModule module;
    module.Put32(0x120 + 20, 0);

    const auto filter = FindBaseInRtti(module.Rtti(), "Derived", "Filter");
    REQUIRE(filter.has_value());
    CHECK(filter->Offset == 8);
    CHECK(filter->Table == nullptr);
}

TEST_CASE("A virtual or repeated MSVC base is refused")
{
    FakeMsvcModule virtualBase;
    virtualBase.PutBase(0x260, 0x60, 8, 0);
    CHECK(FindBaseInRtti(virtualBase.Rtti(), "Derived", "Filter").error().Code == ErrorCode::Unsupported);

    FakeMsvcModule repeated;
    repeated.Put32(0x208, 4);
    repeated.Put32(0x21C, 0x280);
    repeated.PutBase(0x280, 0x60, 16);
    CHECK(FindBaseInRtti(repeated.Rtti(), "Derived", "Filter").error().Code == ErrorCode::Invalid);
}

#endif
