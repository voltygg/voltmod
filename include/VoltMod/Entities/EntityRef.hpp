#pragma once

#include <cstdint>

namespace VoltMod
{

/** Sentinel EHandle value for an unset or cleared handle (INVALID_EHANDLE_INDEX). */
inline constexpr uint32_t InvalidEntityHandle = 0xFFFFFFFFu;

/**
 * @brief A storable reference to an entity: its list index plus the serial number of the entity
 * that occupied it.
 *
 * This is what to keep when a pointer would outlive the frame. Resolving it through
 * @ref EntitySystem::Resolve validates the serial, so a ref whose entity died - or whose index
 * was recycled by a different entity - resolves to nothing rather than to the wrong object.
 */
struct EntityRef
{
    uint32_t Handle = InvalidEntityHandle;

    explicit operator bool() const noexcept { return Handle != InvalidEntityHandle; }
    bool operator==(const EntityRef&) const noexcept = default;
};

}  // namespace VoltMod
