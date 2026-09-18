#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>

namespace VoltMod::Schema
{

/**
 * @brief Compare @ref GeneratedLayout with the live schema.
 *
 * The host does this once for the process. Stale baked offsets would read and write wrong
 * addresses, so a plugin built from the same layout refuses to load when this fails.
 * @return Every mismatch in one message, or ErrorCode::NotReady before the server scope exists.
 */
Status VerifySchemaLayout(ISchemaSystem* schema);

/**
 * @brief Write `addons/voltmod/schema/server.json` for `voltmod schemagen` unless it matches this build.
 *
 * Networked fields come from the engine's serializers, which exist only with @p entities; null
 * writes nothing, which is what happens before the first map.
 */
void WriteSchemaDump(ISchemaSystem* schema, CGameEntitySystem* entities);

}  // namespace VoltMod::Schema
