#include <VoltMod/Entities/EntityRef.hpp>
#include <VoltMod/Ui/UiLayoutRef.hpp>
#include <doctest/doctest.h>

using VoltMod::EntityRef;
using VoltMod::UiLayoutRef;

// Both pairs were read off the wire on build 14178: the handle is what the server spawned, the
// networked value is what the client sent back in the press. They are what pins the encoding.
TEST_CASE("A press names the layout the server spawned")
{
    CHECK(UiLayoutRef{0xEC41AEu}.Is(EntityRef{3386409390u}));
    CHECK(UiLayoutRef{0x440194u}.Is(EntityRef{411566484u}));
}

TEST_CASE("The wire form keeps the index whole and the serial partial")
{
    // Index 430, serial 103345: 14 bits of index, then the serial's low 10 bits (945).
    const UiLayoutRef wire = UiLayoutRef::Of(EntityRef{3386409390u});
    CHECK((wire.Value() & 0x3FFFu) == 430u);
    CHECK((wire.Value() >> 14) == 945u);
}

TEST_CASE("A serial the wire cannot carry does not stop a match")
{
    // Two handles differing only above the ten serial bits collapse to the same press, which is
    // the aliasing the class documents rather than a bug to assert against.
    const EntityRef low{(103345u << 15) | 430u};
    const EntityRef high{((103345u + 1024u) << 15) | 430u};
    CHECK(UiLayoutRef::Of(low) == UiLayoutRef::Of(high));
}

TEST_CASE("A different entity is not a match")
{
    const UiLayoutRef press = UiLayoutRef::Of(EntityRef{3386409390u});
    CHECK_FALSE(press.Is(EntityRef{411566484u}));

    // Same index, a serial that differs inside the ten bits the client does send.
    CHECK_FALSE(press.Is(EntityRef{((103345u + 1u) << 15) | 430u}));
}

TEST_CASE("An unset ref matches no press")
{
    CHECK_FALSE(UiLayoutRef{0xEC41AEu}.Is(EntityRef{}));
    CHECK_FALSE(UiLayoutRef{}.Is(EntityRef{}));
}

TEST_CASE("The largest index the engine allows survives the round trip")
{
    // The list caps at 16384 entries, which is exactly what fourteen bits hold.
    const EntityRef last{(7u << 15) | 16383u};
    CHECK((UiLayoutRef::Of(last).Value() & 0x3FFFu) == 16383u);
    CHECK(UiLayoutRef::Of(last).Is(last));
}
