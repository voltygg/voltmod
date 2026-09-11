#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <cstdint>
#include <span>
#include <string_view>

namespace VoltMod::Schema
{

/** A field layout emitted by the generator. */
struct FieldLayout
{
    std::string_view Name;
    int32_t Offset = 0;
    int32_t Size = 0;
};

/** A class layout and the fields used by generated accessors. */
struct ClassLayout
{
    std::string_view Name;
    int32_t Size = 0;
    int32_t OwnerLinkOffset = -1;
    std::span<const FieldLayout> Fields;
};

/** The layout used to build generated accessors. */
std::span<const ClassLayout> GeneratedLayout();

/** The game build represented by @ref GeneratedLayout. */
std::string_view GeneratedFromBuild();

/** Set the schema system used by verification. Called once by Runtime::Start. */
void BindSchemaVerification(ISchemaSystem* system);

/**
 * @brief Compare @ref GeneratedLayout against the schema the running engine reports.
 *
 * Offsets are baked at build time, so a CS2 update that moves a used class turns every
 * generated accessor into a wrong-address read or write. This is the one check that catches
 * that, and it is why the load aborts rather than degrades on a mismatch.
 *
 * @return Every mismatch in one message, or ErrorCode::NotReady when the schema system has
 *         not populated the server scope yet.
 */
Status VerifySchemaLayout();

}  // namespace VoltMod::Schema
