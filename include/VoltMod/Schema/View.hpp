#pragma once

#include <VoltMod/Engine/EngineTypes.hpp>
#include <cstdint>

namespace VoltMod::Schema
{

/**
 * @brief Base of every generated schema view: the object, plus how to dirty it.
 *
 * A view is a frame-local pointer wrapper over engine memory, never a value to store. Field
 * offsets are baked in at build time by `voltmod schemagen` and checked against the live schema
 * once at load, so a view never resolves anything.
 *
 * @ref Owner and @ref OwnerOffset carry the entity a write has to notify and where this object
 * sits inside it. An entity view is its own owner at offset 0; a sub-object embedded by value
 * inherits the owner and adds its offset, so a write deep inside a struct still dirties the right
 * field of the right entity. A sub-object reached through a pointer has no owner - it lives
 * elsewhere in memory - and replicates through its own `__m_pChainEntity` instead.
 */
class View
{
public:
    View() = default;

    /** A standalone object with no known replication route. */
    explicit View(void* base) noexcept : _base(base) {}

    /** An object at @p ownerOffset inside @p owner. */
    View(void* base, ::CEntityInstance* owner, int32_t ownerOffset) noexcept
        : _base(base), _owner(owner), _ownerOffset(ownerOffset)
    {}

    /** The only validity check, as on Entity and Pawn. */
    explicit operator bool() const noexcept { return _base != nullptr; }

    /** The object this view points at. */
    [[nodiscard]] void* Base() const noexcept { return _base; }

protected:
    void* _base = nullptr;
    ::CEntityInstance* _owner = nullptr;
    int32_t _ownerOffset = 0;
};

}  // namespace VoltMod::Schema
