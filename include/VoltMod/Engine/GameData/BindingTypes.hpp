#pragma once

#include <bit>
#include <cstdint>
#include <cstring>
#include <utility>

namespace VoltMod
{

/** Opaque address for an ABI declared only in its implementation file. */
class Address
{
public:
    Address() = default;
    explicit Address(void* address) noexcept : _address(address) {}

    explicit operator bool() const noexcept { return _address != nullptr; }
    void* Ptr() const noexcept { return _address; }

private:
    void* _address = nullptr;
};

/** Typed engine function. Calling an unbound function is undefined. */
template <class Sig>
class Fn;

template <class Ret, class... Args>
class Fn<Ret(Args...)>
{
public:
    Fn() = default;
    explicit Fn(void* address) noexcept : _address(address) {}

    explicit operator bool() const noexcept { return _address != nullptr; }
    void* Ptr() const noexcept { return _address; }

    Ret operator()(Args... args) const
    {
        return std::bit_cast<Ret (*)(Args...)>(_address)(std::forward<Args>(args)...);
    }

private:
    void* _address = nullptr;
};

/** Typed virtual function called through the object's own vtable; the first parameter is the
 *  object. Calling an unbound function is undefined. */
template <class Sig>
class VirtualFn;

template <class Ret, class Object, class... Args>
class VirtualFn<Ret(Object*, Args...)>
{
public:
    VirtualFn() = default;
    VirtualFn(int index, void* table) noexcept : _index(index), _table(table) {}

    explicit operator bool() const noexcept { return _index >= 0 && _table != nullptr; }
    int Index() const noexcept { return _index; }
    void* Table() const noexcept { return _table; }

    Ret operator()(Object* object, Args... args) const
    {
        auto* vtable = *reinterpret_cast<void***>(object);
        return std::bit_cast<Ret (*)(Object*, Args...)>(vtable[_index])(object, std::forward<Args>(args)...);
    }

private:
    int _index = -1;
    void* _table = nullptr;
};

/** Typed byte offset. Reads and writes allow unaligned fields and do nothing while unbound. */
template <class T>
class OffsetOf
{
public:
    OffsetOf() = default;
    explicit constexpr OffsetOf(int value) noexcept : _value(value) {}

    explicit constexpr operator bool() const noexcept { return _value >= 0; }
    constexpr int Value() const noexcept { return _value; }

    T Read(const void* base) const
    {
        T out{};
        if (_value >= 0 && base)
        {
            std::memcpy(&out, static_cast<const uint8_t*>(base) + _value, sizeof(T));
        }
        return out;
    }

    void Write(void* base, const T& value) const
    {
        if (_value >= 0 && base)
        {
            std::memcpy(static_cast<uint8_t*>(base) + _value, &value, sizeof(T));
        }
    }

private:
    int _value = -1;
};

/** Byte offset for an embedded type declared only in an implementation file. */
template <>
class OffsetOf<void>
{
public:
    OffsetOf() = default;
    explicit constexpr OffsetOf(int value) noexcept : _value(value) {}

    explicit constexpr operator bool() const noexcept { return _value >= 0; }
    constexpr int Value() const noexcept { return _value; }

    const void* Ptr(const void* base) const
    {
        if (_value < 0 || !base)
        {
            return nullptr;
        }
        return static_cast<const uint8_t*>(base) + _value;
    }

private:
    int _value = -1;
};

}  // namespace VoltMod
