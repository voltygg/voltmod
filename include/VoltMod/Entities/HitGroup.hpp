#pragma once

namespace VoltMod
{

/**
 * Engine hitgroup ids, as carried by the hitbox the damage trace struck
 * (CTakeDamageInfo::m_pTrace->m_pHitBox->m_nGroupId). Damage with no trace - fire, the bomb, a
 * fall - has no hitgroup at all and reads @ref Invalid; CTakeDamageInfo::m_iHitGroupId is not the
 * source, it reads -1 even for ordinary bullet damage.
 *
 * Deliberately kept in its own dependency-free header: consumers that only need the vocabulary
 * (damage rules, hit statistics) can include it without pulling in the hook, its callback
 * registry, or anything that has to be linked - which is what lets SDK-free translation units
 * share these values instead of restating them.
 */
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
