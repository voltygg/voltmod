#include <CS2Kit/Utils/AngleMath.hpp>
#include <cmath>
#include <doctest/doctest.h>

using namespace CS2Kit::Utils::AngleMath;

namespace
{
struct Vec3
{
    float x, y, z;
};

bool Near(float a, float b, float eps = 0.01f)
{
    return std::fabs(a - b) < eps;
}
}  // namespace

TEST_CASE("NormalizeAngleDelta wraps to -180 exclusive through 180 inclusive")
{
    CHECK(Near(NormalizeAngleDelta(0.0f), 0.0f));
    CHECK(Near(NormalizeAngleDelta(179.0f), 179.0f));
    CHECK(Near(NormalizeAngleDelta(180.0f), 180.0f));
    CHECK(Near(NormalizeAngleDelta(181.0f), -179.0f));
    CHECK(Near(NormalizeAngleDelta(-180.0f), 180.0f));
    CHECK(Near(NormalizeAngleDelta(360.0f), 0.0f));
    CHECK(Near(NormalizeAngleDelta(720.0f + 45.0f), 45.0f));
    CHECK(Near(NormalizeAngleDelta(-540.0f), 180.0f));
}

TEST_CASE("AnglesToPoint cardinal directions")
{
    Vec3 origin{0, 0, 0};

    auto east = AnglesToPoint(origin, Vec3{100, 0, 0});
    CHECK(Near(east.Yaw, 0.0f));
    CHECK(Near(east.Pitch, 0.0f));

    auto north = AnglesToPoint(origin, Vec3{0, 100, 0});
    CHECK(Near(north.Yaw, 90.0f));

    auto west = AnglesToPoint(origin, Vec3{-100, 0, 0});
    CHECK(Near(std::fabs(west.Yaw), 180.0f));

    // Source pitch is positive looking down.
    auto up = AnglesToPoint(origin, Vec3{100, 0, 100});
    CHECK(up.Pitch < 0.0f);
    auto down = AnglesToPoint(origin, Vec3{100, 0, -100});
    CHECK(down.Pitch > 0.0f);
    CHECK(Near(down.Pitch, 45.0f));
}

TEST_CASE("AngularDistance combines wrapped components")
{
    AimAngles a{.Pitch = 0.0f, .Yaw = 179.0f};
    AimAngles b{.Pitch = 0.0f, .Yaw = -179.0f};
    CHECK(Near(AngularDistance(a, b), 2.0f));

    AimAngles c{.Pitch = 3.0f, .Yaw = 0.0f};
    AimAngles d{.Pitch = 0.0f, .Yaw = 4.0f};
    CHECK(Near(AngularDistance(c, d), 5.0f));
}
