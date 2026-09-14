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

/** The schema system that verification and the dump read. Set once by Runtime::Start. */
void BindSchemaVerification(ISchemaSystem* system);

/**
 * @brief Compare @ref GeneratedLayout with the live schema.
 *
 * Stale baked offsets would read and write wrong addresses, so a mismatch aborts the load.
 * @return Every mismatch in one message, or ErrorCode::NotReady before the server scope exists.
 */
Status VerifySchemaLayout();

/**
 * @brief Write `addons/voltmod/schema/server.json` for `voltmod schemagen` unless it matches this build.
 *
 * Networked fields come from the engine's serializers, which exist only with @p entities; null writes nothing.
 */
void WriteSchemaDump(CGameEntitySystem* entities);

}  // namespace VoltMod::Schema
