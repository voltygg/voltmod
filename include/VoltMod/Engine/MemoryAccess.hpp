#pragma once

#include <cstddef>
#include <cstdint>

namespace VoltMod
{

/**
 * @file MemoryAccess.hpp
 * @brief Typed read/write of engine fields at a base pointer + byte offset.
 *
 * Engine objects have no nameable C++ type, so fields are reached by offset.
 * These helpers keep that one `reinterpret_cast` in a single audited place; the
 * caller owns @p base and @p offset. Note this aliases live storage - not a
 * `std::bit_cast`, which copies bits into a new object.
 */

/** Pointer to the field of type T located @p offset bytes into @p base. */
template <typename T>
[[nodiscard]] T* MemberPtr(void* base, std::ptrdiff_t offset) noexcept
{
    return reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}

/** Read the value of type T located @p offset bytes into @p base. */
template <typename T>
[[nodiscard]] T ReadAt(void* base, std::ptrdiff_t offset) noexcept
{
    return *MemberPtr<T>(base, offset);
}

/** Write @p value to the field of type T located @p offset bytes into @p base. */
template <typename T>
void WriteAt(void* base, std::ptrdiff_t offset, const T& value) noexcept
{
    *MemberPtr<T>(base, offset) = value;
}

}  // namespace VoltMod
