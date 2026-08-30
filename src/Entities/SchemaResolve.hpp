#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Entities/Field.hpp>
#include <string_view>

namespace VoltMod
{

/**
 * @brief One schema query's answer, as the backend sees it.
 *
 * `Available == false` means the schema system cannot answer yet. That is "ask again later",
 * never a miss, and @ref ResolveField must not cache it. `Available && !Found` is a real miss and
 * is cached like a hit.
 */
struct FieldQueryResult
{
    bool Available = false;
    bool Found = false;
    FieldRef Ref;
};

/** How @ref ResolveField asks the engine. A plain function pointer so the seam costs nothing. */
using FieldQueryFn = FieldQueryResult (*)(std::string_view klass, std::string_view field);

/**
 * Install the backend @ref ResolveField queries. Passing nullptr restores the default, which
 * reports the schema system as unavailable.
 *
 * Set once at load by @ref BindSchemaSystem; tests install a fake to drive the caching rules
 * without the engine. Not thread-safe, and neither is the cache behind it: game thread only.
 */
void SetFieldQuery(FieldQueryFn query);

/** Drop every cached answer. Only tests need this - schema offsets are process constants. */
void ResetFieldCache();

/**
 * Point the resolver at the engine's ISchemaSystem. Called once by Runtime::Start.
 * @return Error::NotReady when the engine did not hand the interface over; the resolver then
 *         keeps answering "pending" and every field read degrades to a zero value.
 */
Status BindSchemaSystem(ISchemaSystem* system);

}  // namespace VoltMod
