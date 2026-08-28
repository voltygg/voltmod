#pragma once

#include <VoltMod/Entities/EntityRef.hpp>
#include <cstdint>

namespace VoltMod
{

/**
 * @brief The layout a press came from, as the client names it.
 *
 * Deliberately not an @ref EntityRef. A press carries the entity's *networked* handle - 14 bits of
 * index and only the low 10 bits of the serial - so the rest of the serial an @ref EntityRef holds
 * never reaches the server and a full handle cannot be rebuilt from it. Ask @ref Is which entity
 * this is instead of comparing handles.
 *
 * 14 bits is the whole index: the engine caps the entity list at 16384 entries. The 10 serial bits
 * are what makes a press from a layout that has since been removed and its index recycled fail to
 * match, which is the check that matters; 1 in 1024 such presses can still alias, so a handler
 * must tolerate a stale press rather than trust it blindly.
 */
class UiLayoutRef
{
public:
    UiLayoutRef() = default;
    explicit constexpr UiLayoutRef(uint32_t networked) noexcept : _networked(networked) {}

    /** The handle exactly as it arrived, for logging. Not an @ref EntityRef::Handle. */
    [[nodiscard]] constexpr uint32_t Value() const noexcept { return _networked; }

    /** The form @p ref takes on the wire, which is what a press can be compared against. */
    [[nodiscard]] static constexpr UiLayoutRef Of(EntityRef ref) noexcept
    {
        return UiLayoutRef((ref.Handle & IndexMask) | (((ref.Handle >> SerialShift) & SerialMask) << IndexBits));
    }

    /** True when @p ref is the entity this press names, to the 24 bits the client sent. */
    [[nodiscard]] constexpr bool Is(EntityRef ref) const noexcept
    {
        return static_cast<bool>(ref) && Of(ref)._networked == _networked;
    }

    bool operator==(const UiLayoutRef&) const noexcept = default;

private:
    static constexpr uint32_t IndexBits = 14;
    static constexpr uint32_t IndexMask = (1u << IndexBits) - 1;
    static constexpr uint32_t SerialMask = (1u << 10) - 1;

    /** Where @ref EntityRef::Handle keeps the serial; its index is the low 15 bits. */
    static constexpr uint32_t SerialShift = 15;

    uint32_t _networked = 0;
};

}  // namespace VoltMod
