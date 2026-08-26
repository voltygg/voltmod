#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace VoltMod::Engine
{
struct Interfaces;
}  // namespace VoltMod::Engine

namespace VoltMod::Entities
{

/**
 * Runtime schema field offset resolution via ISchemaSystem.
 * Provides access to entity field offsets at runtime by querying the
 * engine's schema system. Results are cached for O(1) repeated access.
 */
class SchemaService
{
public:
    /** @p interfaces supplies ISchemaSystem; it must outlive this service. */
    explicit SchemaService(Engine::Interfaces& interfaces) : _interfaces(interfaces) {}

    bool Initialize();

    /**
     * Field offset via the engine's schema system (cached). When `expectedSize` > 0, the
     * first (uncached) lookup also compares the engine's field size against it and warns
     * on mismatch - catches schema drift after a game update. Warning-only.
     *
     * Pass `sizeof(T)` whenever the caller reads or writes a T at exactly this offset;
     * a mismatch then means the access itself is wrong. Leave it 0 when the offset is
     * not that: an embedded subobject whose address is taken, an offset added to an
     * inner field's, or a fixed-size buffer read through Engine::MemberPtr. Guessing there
     * produces warnings that are always wrong, on the fields most likely to really move.
     */
    int GetOffset(const char* className, const char* fieldName, int expectedSize = 0);

    /** @ref GetOffset for a field accessed as exactly a T - the drift check derives its expected
     *  size from the type being read or written, so the two cannot fall out of step. Use the
     *  size-less @ref GetOffset for the cases above where sizeof(T) is not what is at the offset. */
    template <class T>
    int GetOffsetOf(const char* className, const char* fieldName)
    {
        return GetOffset(className, fieldName, static_cast<int>(sizeof(T)));
    }

private:
    /** `std::less<>` so a lookup by `const char*` compares against a string_view instead of
     *  materializing a std::string for the class and the field on every cache hit - field names
     *  like "m_bOnGroundLastTick" are past the small-string buffer, so those were real allocations
     *  on the hot path. */
    std::map<std::string, std::map<std::string, int, std::less<>>, std::less<>> _offsetCache;
    Engine::Interfaces& _interfaces;
};

}  // namespace VoltMod::Entities
