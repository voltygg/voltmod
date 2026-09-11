#include <VoltMod/Engine/MemoryAccess.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <string>

using VoltMod::MemberPtr;
using VoltMod::ReadAt;
using VoltMod::WriteAt;

// Stand-in for an engine object: standard-layout so offsetof mirrors how the
// real code reaches fields by their schema/gamedata byte offset.
struct Sample
{
    uint8_t Mode;
    uint32_t Color;
    uint64_t Buttons;
    void* Next;
    char Name[16];
};

TEST_CASE("MemoryAccess::ReadAt and WriteAt reach fields at their offsets, whatever the width")
{
    Sample s{};
    WriteAt<uint8_t>(&s, offsetof(Sample, Mode), static_cast<uint8_t>(0x7F));
    WriteAt<uint32_t>(&s, offsetof(Sample, Color), 0xDEADBEEFu);
    WriteAt<uint64_t>(&s, offsetof(Sample, Buttons), 0xFEEDFACECAFEBEEFull);

    CHECK_EQ(s.Mode, static_cast<uint8_t>(0x7F));
    CHECK_EQ(s.Color, 0xDEADBEEFu);
    CHECK_EQ(s.Buttons, 0xFEEDFACECAFEBEEFull);

    CHECK_EQ(ReadAt<uint8_t>(&s, offsetof(Sample, Mode)), static_cast<uint8_t>(0x7F));
    CHECK_EQ(ReadAt<uint32_t>(&s, offsetof(Sample, Color)), 0xDEADBEEFu);
    CHECK_EQ(ReadAt<uint64_t>(&s, offsetof(Sample, Buttons)), 0xFEEDFACECAFEBEEFull);
}

TEST_CASE("MemoryAccess::MemberPtr points into the object itself")
{
    Sample s{};
    std::memcpy(s.Name, "hello", 6);

    auto* color = MemberPtr<uint32_t>(&s, offsetof(Sample, Color));
    CHECK(color == &s.Color);
    *color = 0x99u;
    CHECK_EQ(s.Color, 0x99u);

    CHECK_EQ(std::string(MemberPtr<const char>(&s, offsetof(Sample, Name))), std::string("hello"));
}

TEST_CASE("MemoryAccess::ReadAt/WriteAt round-trip a pointer field")
{
    Sample s{};
    int target = 42;

    WriteAt<void*>(&s, offsetof(Sample, Next), &target);
    CHECK(s.Next == &target);

    auto* got = ReadAt<void*>(&s, offsetof(Sample, Next));
    CHECK(got == &target);
    CHECK_EQ(*static_cast<int*>(got), 42);
}
