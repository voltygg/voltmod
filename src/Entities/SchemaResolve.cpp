#include "Entities/SchemaResolve.hpp"

#include <VoltMod/Core/Log.hpp>
#include <unordered_map>

namespace VoltMod
{

// Schema offsets are constants of the loaded server binary: the same class and field resolve to
// the same offset for every plugin, every Runtime and every map in the process. That is why this
// cache is a file-static rather than a service - there is nothing per-load about it, and making it
// per-Runtime would only mean resolving the same offsets once per plugin. Game thread only; the
// map is unsynchronized.
static std::unordered_map<uint64_t, FieldRef> g_fields;

/** Answers "the schema system is not up" until BindSchemaSystem installs the real backend. */
static FieldQueryResult QueryUnavailable(std::string_view, std::string_view)
{
    return {};
}

static FieldQueryFn g_query = &QueryUnavailable;

void SetFieldQuery(FieldQueryFn query)
{
    g_query = query ? query : &QueryUnavailable;
}

void ResetFieldCache()
{
    g_fields.clear();
}

const FieldRef& PendingField() noexcept
{
    // Its address is the signal, so it must be one object for the process.
    static const FieldRef pending{};
    return pending;
}

const FieldRef& ResolveField(std::string_view klass, std::string_view field, size_t expectedSize)
{
    const uint64_t key = FieldKey(klass, field);
    if (auto it = g_fields.find(key); it != g_fields.end())
        return it->second;

    const FieldQueryResult answer = g_query(klass, field);
    if (!answer.Available)
    {
        // Not a miss - nothing to cache, and the caller is expected to ask again once the engine
        // has handed the schema system over.
        return PendingField();
    }

    if (!answer.Found)
    {
        Log::Warn("Schema: field '{}' not found on '{}' or any of its base classes.", field, klass);
        // Cached anyway: a name that is wrong now stays wrong, and one warning beats one per call.
        return g_fields.emplace(key, FieldRef{}).first->second;
    }

    if (expectedSize > 0 && answer.Ref.Size > 0 && static_cast<size_t>(answer.Ref.Size) != expectedSize)
    {
        Log::Warn("Schema: {}::{} is {} bytes but the caller reads {} (schema drift?).", klass, field, answer.Ref.Size,
                  expectedSize);
    }

    // Reference stability across rehashing is why this is unordered_map and not a flat table: the
    // FieldRef a Field<> holds on to has to stay put for the process.
    return g_fields.emplace(key, answer.Ref).first->second;
}

}  // namespace VoltMod
