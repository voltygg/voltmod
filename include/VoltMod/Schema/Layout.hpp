#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <cstdint>
#include <span>
#include <string_view>

namespace VoltMod::Schema
{

/** One baked field: what the generator read out of the dump. */
struct FieldLayout
{
    std::string_view Name;
    int32_t Offset = 0;
    int32_t Size = 0;
};

/** One baked class, with the fields the generated accessors reach. */
struct ClassLayout
{
    std::string_view Name;
    int32_t Size = 0;
    int32_t ChainOffset = -1;
    std::span<const FieldLayout> Fields;
};

/** The layout the generated accessors were built against. */
std::span<const ClassLayout> GeneratedLayout();

/** Hand the schema system to the verifier. Called once by Runtime::Start. */
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
