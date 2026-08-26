#include <VoltMod/Engine/MemoryAccess.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <string>

using VoltMod::Engine::MemberPtr;
using VoltMod::Engine::ReadAt;
using VoltMod::Engine::WriteAt;

namespace
{
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
}  // namespace

TEST_CASE("MemoryAccess::ReadAt reads fields at their offsets")
{
    Sample s{};
    s.Mode = 0x12;
    s.Color = 0xAABBCCDDu;
    s.Buttons = 0x1122334455667788ull;

    CHECK_EQ(ReadAt<uint8_t>(&s, offsetof(Sample, Mode)), static_cast<uint8_t>(0x12));
    CHECK_EQ(ReadAt<uint32_t>(&s, offsetof(Sample, Color)), 0xAABBCCDDu);
    CHECK_EQ(ReadAt<uint64_t>(&s, offsetof(Sample, Buttons)), 0x1122334455667788ull);
}

TEST_CASE("MemoryAccess::WriteAt writes fields at their offsets")
{
    Sample s{};
    WriteAt<uint8_t>(&s, offsetof(Sample, Mode), static_cast<uint8_t>(0x7F));
    WriteAt<uint32_t>(&s, offsetof(Sample, Color), 0xDEADBEEFu);
    WriteAt<uint64_t>(&s, offsetof(Sample, Buttons), 0xFEEDFACECAFEBEEFull);

    CHECK_EQ(s.Mode, static_cast<uint8_t>(0x7F));
    CHECK_EQ(s.Color, 0xDEADBEEFu);
    CHECK_EQ(s.Buttons, 0xFEEDFACECAFEBEEFull);
}

TEST_CASE("MemoryAccess::MemberPtr yields a pointer into the object")
{
    Sample s{};

    auto* p = MemberPtr<uint32_t>(&s, offsetof(Sample, Color));
    CHECK(p == &s.Color);

    *p = 0x99u;
    CHECK_EQ(s.Color, 0x99u);
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

TEST_CASE("MemoryAccess::MemberPtr<const char> reads a string field")
{
    Sample s{};
    std::memcpy(s.Name, "hello", 6);

    const char* p = MemberPtr<const char>(&s, offsetof(Sample, Name));
    CHECK_EQ(std::string(p), std::string("hello"));
}
