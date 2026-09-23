#pragma once

namespace VoltMod
{

/** The body part a bullet hit. Damage with no bullet, such as fire, the bomb or a fall, is
 *  @ref Invalid. */
enum class HitGroup : int
{
    Invalid = -1,
    Generic = 0,
    Head = 1,
    Chest = 2,
    Stomach = 3,
    LeftArm = 4,
    RightArm = 5,
    LeftLeg = 6,
    RightLeg = 7,
    Neck = 8,
};

}  // namespace VoltMod
