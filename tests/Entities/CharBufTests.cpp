#include <VoltMod/Core/CharBuf.hpp>
#include <cstring>
#include <doctest/doctest.h>
#include <ostream>
#include <string>

using VoltMod::CharBuf;

TEST_CASE("CharBuf starts empty and zeroed")
{
    CharBuf<8> buf;
    CHECK(buf.Empty());
    CHECK(buf.View().empty());
    CHECK(buf.Str().empty());
    for (char c : buf.Value)
        CHECK(c == '\0');
}

TEST_CASE("CharBuf assignment NUL-terminates and zeroes the tail")
{
    CharBuf<8> buf;
    buf = "abc";
    CHECK(buf.View() == "abc");
    CHECK(buf.Value[3] == '\0');

    // A shorter value must not leave the tail of the longer one behind: the engine reads the
    // buffer up to the first NUL, but anything walking the whole field would still see it.
    buf = "abcdefg";
    buf = "xy";
    CHECK(buf.View() == "xy");
    for (size_t i = 2; i < 8; ++i)
        CHECK(buf.Value[i] == '\0');
}

TEST_CASE("CharBuf truncates to N-1 characters plus a NUL")
{
    CharBuf<8> buf{"0123456789"};
    CHECK(buf.View() == "0123456");
    CHECK(buf.View().size() == 7);
    CHECK(buf.Value[7] == '\0');

    // Exactly N-1 fits whole.
    CharBuf<8> exact{"0123456"};
    CHECK(exact.View() == "0123456");
    CHECK(exact.Value[7] == '\0');
}

TEST_CASE("CharBuf reads back a value the engine wrote without a terminator")
{
    // A full buffer with no room for a NUL: View stops at the end rather than running off it.
    CharBuf<4> buf;
    std::memcpy(buf.Value, "abcd", 4);
    CHECK(buf.View() == "abcd");
    CHECK(buf.View().size() == 4);
    CHECK(buf.Str() == "abcd");
}

TEST_CASE("CharBuf converts from the shapes call sites have")
{
    const std::string name = "player";
    CharBuf<16> fromString{name};
    CharBuf<16> fromLiteral{"player"};
    CharBuf<16> fromView{std::string_view{name}};

    CHECK(fromString.View() == "player");
    CHECK(fromLiteral.View() == "player");
    CHECK(fromView.View() == "player");

    // A null char* is an empty buffer, not a crash: engine strings can be absent.
    CharBuf<16> fromNull{static_cast<const char*>(nullptr)};
    CHECK(fromNull.Empty());
}

TEST_CASE("CharBuf is trivially copyable so it can be read and written whole")
{
    // Field<CharBuf<N>> reads and writes the buffer with ReadAt/WriteAt, which alias engine
    // storage; a non-trivial type there would be undefined behaviour rather than a slow path.
    static_assert(std::is_trivially_copyable_v<CharBuf<128>>);
    static_assert(sizeof(CharBuf<128>) == 128);
}
