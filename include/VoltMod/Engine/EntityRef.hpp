#pragma once

#include <cstdint>

namespace VoltMod
{

/**
 * @brief A storable reference to an entity: its list index plus the serial number of the entity
 * that occupied it.
 *
 * This is what to keep when a pointer would outlive the frame. Resolving it through
 * @ref EntitySystem::Resolve validates the serial, so a ref whose entity died - or whose index
 * was recycled by a different entity - resolves to nothing rather than to the wrong object.
 * Generated handle fields such as `OwnerRef()` return one; `EntityRef{}` clears a handle.
 */
struct EntityRef
{
    /** INVALID_EHANDLE_INDEX. */
    static constexpr uint32_t Unset = 0xFFFFFFFFu;

    /** The engine's EHandle bits. */
    uint32_t Handle = Unset;

    explicit operator bool() const noexcept { return Handle != Unset; }
    bool operator==(const EntityRef&) const noexcept = default;
};

// Generated accessors read and write it in place of the engine's 4-byte CHandle.
static_assert(sizeof(EntityRef) == 4);

}  // namespace VoltMod
