#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace CS2Kit::Sdk
{

/**
 * Runtime schema field offset resolution via ISchemaSystem.
 * Provides access to entity field offsets at runtime by querying the
 * engine's schema system. Results are cached for O(1) repeated access.
 */
class SchemaService
{
public:
    SchemaService() = default;

    bool Initialize();

    /**
     * Field offset via the engine's schema system (cached). When `expectedSize` > 0, the
     * first (uncached) lookup also compares the engine's field size against it and warns
     * on mismatch - catches schema drift after a game update. Warning-only.
     */
    int GetOffset(const char* className, const char* fieldName, int expectedSize = 0);

private:
    /** `std::less<>` so a lookup by `const char*` compares against a string_view instead of
     *  materializing a std::string for the class and the field on every cache hit - field names
     *  like "m_bOnGroundLastTick" are past the small-string buffer, so those were real allocations
     *  on the hot path. */
    std::map<std::string, std::map<std::string, int, std::less<>>, std::less<>> _offsetCache;
};

}  // namespace CS2Kit::Sdk
