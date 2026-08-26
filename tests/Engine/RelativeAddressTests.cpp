#include <VoltMod/Engine/RelativeAddress.hpp>
#include <cstdint>
#include <doctest/doctest.h>

using VoltMod::Rel32ReadInBounds;
using VoltMod::Rel32Site;
using VoltMod::Rel32Size;
using VoltMod::Rel32Target;

// A plausible mapped image; nothing is dereferenced, only the arithmetic is checked.
static constexpr uintptr_t Base = 0x140000000ull;
static constexpr size_t Size = 0x1000;

TEST_CASE("Rel32Site is the match plus the displacement's distance into the instruction")
{
    CHECK(Rel32Site(Base, 0) == Base);
    CHECK(Rel32Site(Base, 3) == Base + 3);
    CHECK(Rel32Site(Base + 0x20, 98) == Base + 0x20 + 98);
}

TEST_CASE("Rel32Target resolves relative to the byte after the displacement")
{
    // lea rax, [rip+0x10] whose operand sits 3 bytes in: target is site + 4 + 0x10.
    CHECK(Rel32Target(Rel32Site(Base, 3), 0x10) == Base + 3 + Rel32Size + 0x10);

    // A backwards reference: the displacement is signed.
    CHECK(Rel32Target(Base + 0x100, -0x40) == Base + 0x100 + Rel32Size - 0x40);

    // The displacement can land exactly on the instruction that carried it.
    CHECK(Rel32Target(Base + 0x10, -Rel32Size) == Base + 0x10);
}

TEST_CASE("Rel32Target honours a wider instruction tail")
{
    // An instruction with a trailing immediate: the operand ends 8 bytes after the site.
    CHECK(Rel32Target(Base, 0x20, 8) == Base + 8 + 0x20);
}

TEST_CASE("Rel32ReadInBounds accepts a displacement that fits inside the image")
{
    CHECK(Rel32ReadInBounds(Base, Size, Base, 0));
    CHECK(Rel32ReadInBounds(Base, Size, Base, 98));
    // The last four bytes of the mapping are still readable.
    CHECK(Rel32ReadInBounds(Base, Size, Base + Size - Rel32Size, 0));
}

TEST_CASE("Rel32ReadInBounds rejects a read that would leave the image")
{
    // Three bytes left is not four.
    CHECK_FALSE(Rel32ReadInBounds(Base, Size, Base + Size - 3, 0));
    // The site itself is past the end.
    CHECK_FALSE(Rel32ReadInBounds(Base, Size, Base + Size, 0));
    // A rel32At larger than the distance to the end.
    CHECK_FALSE(Rel32ReadInBounds(Base, Size, Base + Size - 8, 8));
    // A match that is not in this module at all.
    CHECK_FALSE(Rel32ReadInBounds(Base, Size, Base - 1, 0));
    // A negative offset can only come from hand-edited gamedata, and is never a read.
    CHECK_FALSE(Rel32ReadInBounds(Base, Size, Base + 0x10, -4));
    // An empty mapping has nothing to read.
    CHECK_FALSE(Rel32ReadInBounds(Base, 0, Base, 0));
}

TEST_CASE("Rel32ReadInBounds rejects an offset that overflows the address space")
{
    constexpr uintptr_t high = UINTPTR_MAX - 0x10;
    CHECK_FALSE(Rel32ReadInBounds(high, 0x10, high, 0x7FFFFFFF));
}
