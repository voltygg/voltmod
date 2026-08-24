#pragma once

#include <bit>
#include <cstdint>

namespace VoltMod::Sdk
{

/**
 * Call a virtual function by vtable index on a given object.
 */
template <typename Ret, typename... Args>
constexpr Ret CallVirtual(int index, void* thisPtr, Args... args) noexcept
{
    auto vtable = *reinterpret_cast<void***>(thisPtr);
    auto fn = std::bit_cast<Ret (*)(void*, Args...)>(vtable[index]);
    return fn(thisPtr, args...);
}

}  // namespace VoltMod::Sdk
