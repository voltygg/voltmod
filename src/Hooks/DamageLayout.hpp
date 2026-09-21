#pragma once

#include <cstddef>
#include <cstdint>
#include <entityhandle.h>
#include <gametrace.h>
#include <mathlib/vector.h>

namespace VoltMod
{

/** CTakeDamageInfo as of server build 2000908, from CS2Fixes. Only the leading fields are named;
 *  the engine's constructor fills the rest. */
struct EngineDamageInfo
{
    void* VTable;
    Vector Force;
    Vector Position;
    Vector ReportedPosition;
    Vector Direction;
    CEntityHandle Inflictor;
    CEntityHandle Attacker;
    CEntityHandle Ability;
    float Damage;
    float TotalledDamage;
    int32_t DamageType;
    uint8_t Rest[200];
};
static_assert(offsetof(EngineDamageInfo, Inflictor) == 0x38);
static_assert(offsetof(EngineDamageInfo, Damage) == 0x44);
static_assert(offsetof(EngineDamageInfo, DamageType) == 0x4C);
static_assert(sizeof(EngineDamageInfo) == 280);

/** CTakeDamageResult as of server build 2000908, from CS2Fixes. */
struct EngineDamageResult
{
    EngineDamageInfo* OriginatingInfo;
    uint8_t DestructibleHitGroupRequests[16];
    int32_t HealthLost;
    int32_t HealthBefore;
    float DamageDealt;
    float PreModifiedDamage;
    Vector DamagePosition;
    int32_t TotalledHealthLost;
    float TotalledDamageDealt;
    float TotalledPreModifiedDamage;
    float NewDamageAccumulatorValue;
    uint64_t DamageFlags;
    bool WasDamageSuppressed;
    bool SuppressFlinch;
    HitGroup_t OverrideFlinchHitGroup;
    uint8_t Unknown[8];
};
static_assert(offsetof(EngineDamageResult, DamageFlags) == 72);
static_assert(sizeof(EngineDamageResult) == 96);

}  // namespace VoltMod
