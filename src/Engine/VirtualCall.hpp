#pragma once

#include <bit>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace VoltMod::Engine
{

/**
 * Call a virtual function by vtable index on a given object.
 *
 * The signature is rebuilt from the decayed argument types: the game's vfuncs take their
 * parameters by value, so an lvalue argument must not deduce a reference parameter here -
 * that would pass its address where the callee expects the value.
 */
template <typename Ret, typename... Args>
Ret CallVirtual(int index, void* thisPtr, Args&&... args)
{
    auto vtable = *reinterpret_cast<void***>(thisPtr);
    auto fn = std::bit_cast<Ret (*)(void*, std::decay_t<Args>...)>(vtable[index]);
    return fn(thisPtr, std::forward<Args>(args)...);
}

}  // namespace VoltMod::Engine
