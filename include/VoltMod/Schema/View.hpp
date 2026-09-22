#pragma once

#include <VoltMod/Engine/EngineTypes.hpp>
#include <cstdint>

namespace VoltMod::Schema
{

/**
 * @brief Base of every generated schema view: a frame-local pointer into engine memory, never stored.
 *
 * The owner is the entity a write notifies, at the owner offset: an entity owns itself at 0, a struct
 * embedded by value adds its offset, and a sub-object behind a pointer has no owner and notifies
 * through its own `__m_pChainEntity` link.
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
    void* Base() const noexcept { return _base; }

protected:
    void* _base = nullptr;
    ::CEntityInstance* _owner = nullptr;
    int32_t _ownerOffset = 0;
};

}  // namespace VoltMod::Schema
