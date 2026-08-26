#pragma once

#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/MemoryAccess.hpp>
#include <algorithm>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>

namespace VoltMod
{

/**
 * @brief Where one schema field sits on one class.
 *
 * Offsets are constants of the loaded server binary, so a resolved ref is good for the whole
 * process. `Offset < 0` means "no answer": either the field does not exist on the class or the
 * schema system was not up when it was asked. @ref PendingField tells those two apart.
 */
struct FieldRef
{
    int32_t Offset = -1;       ///< Byte offset from the entity, or -1 when unresolved.
    int32_t Size = 0;          ///< The engine's own size for the field, for the drift check.
    bool Networked = false;    ///< The field replicates, so a write must dirty it.
    int32_t ChainOffset = -1;  ///< `__m_pChainEntity` on the class, or -1 when it has none.

    explicit operator bool() const noexcept { return Offset >= 0; }
};

/**
 * FNV-1a 64 over `class::field`. The cache key @ref ResolveField uses; public so a test can
 * pin the mapping and so framework code can key its own tables the same way.
 */
[[nodiscard]] constexpr uint64_t FieldKey(std::string_view klass, std::string_view field) noexcept
{
    constexpr uint64_t offsetBasis = 14695981039346656037ull;
    constexpr uint64_t prime = 1099511628211ull;

    uint64_t hash = offsetBasis;
    auto mix = [&hash](std::string_view text) {
        for (char c : text)
        {
            hash ^= static_cast<uint8_t>(c);
            hash *= prime;
        }
    };

    mix(klass);
    mix("::");
    mix(field);
    return hash;
}

/**
 * Resolve @p field on @p klass once for the process, walking toward base classes so a field the
 * schema declares on a parent answers when asked for on the child. The walk only goes that way:
 * a field CS2 declares on a derived class (`CCSPlayerPawn::m_angEyeAngles`) has to be asked for
 * under that name, and asking its base resolves nothing.
 *
 * Hits *and* misses are cached, so a wrong name costs one schema walk rather than one per call.
 * A lookup made before the engine is ready to answer - the schema system or the entity system's
 * network serializer database is missing, which is the case until the first map load - is not an
 * answer: it returns @ref PendingField without caching, and the caller is expected to ask again.
 * Caching there would freeze `Networked = false` and silently stop writes from replicating. When
 * @p expectedSize is non-zero the
 * first (uncached) lookup compares it against the engine's own field size and warns once on a
 * mismatch - the signal that a game update moved or retyped the field. Game-thread only.
 *
 * @return A reference stable for the process lifetime.
 */
const FieldRef& ResolveField(std::string_view klass, std::string_view field, size_t expectedSize);

/**
 * The ref @ref ResolveField hands back while the engine cannot answer yet. Compare addresses
 * against it to tell "ask again later" from a cached miss; both are falsy.
 */
const FieldRef& PendingField() noexcept;

/**
 * Tell the engine a networked field written through @ref WriteAt changed, so it replicates on the
 * next snapshot instead of riding whatever touches the entity next. No-op for a null entity, an
 * unresolved ref, or a field that does not replicate.
 */
void MarkChanged(CEntityInstance* entity, const FieldRef& ref);

/**
 * @brief A field offset resolved on first use, for code with no entity wrapper to hang a
 * @ref Field on: a field of an engine sub-object, or a one-off internal lookup.
 *
 * Declare one `static` beside the code that uses it, with string literals for the names. It
 * resolves once and keeps retrying for as long as the schema system is not up yet, so an instance
 * created during load does not freeze an empty answer. Game-thread only.
 *
 * ```cpp
 * static const LazyField kItemServices{"CBasePlayerPawn", "m_pItemServices", sizeof(void*)};
 * if (kItemServices)
 *     services = ReadAt<void*>(pawn, kItemServices->Offset);
 * ```
 */
class LazyField
{
public:
    /** @p klass and @p field must outlive this object; pass string literals. @p expectedSize is
     *  the drift check, as on @ref ResolveField - 0 skips it. */
    constexpr LazyField(std::string_view klass, std::string_view field, size_t expectedSize = 0) noexcept
        : _klass(klass), _field(field), _expectedSize(expectedSize)
    {}

    [[nodiscard]] const FieldRef& Ref() const
    {
        if (_ref == nullptr || _ref == &PendingField())
            _ref = &ResolveField(_klass, _field, _expectedSize);
        return *_ref;
    }

    const FieldRef& operator*() const { return Ref(); }
    const FieldRef* operator->() const { return &Ref(); }
    explicit operator bool() const { return static_cast<bool>(Ref()); }

private:
    std::string_view _klass;
    std::string_view _field;
    size_t _expectedSize;
    mutable const FieldRef* _ref = nullptr;
};

/** A string literal usable as a non-type template argument. */
template <size_t N>
struct FixedString
{
    char Value[N]{};

    constexpr FixedString(const char (&text)[N]) { std::copy_n(text, N, Value); }

    /** The literal without its terminating NUL. */
    [[nodiscard]] constexpr std::string_view View() const { return {Value, N - 1}; }
};

/**
 * @brief A fixed char array engine field (`char m_name[N]`), read and written whole.
 *
 * Assignment truncates to N-1 characters and always NUL-terminates, and the unused tail is
 * zeroed so no remnant of the previous value stays in the entity. @ref View borrows from this
 * object, so keep the buffer alive for as long as the view; @ref Str copies.
 */
template <size_t N>
struct CharBuf
{
    static_assert(N > 1, "a CharBuf needs room for at least one character and a NUL");

    char Value[N]{};

    CharBuf() = default;
    CharBuf(std::string_view text) noexcept { Assign(text); }
    CharBuf(const char* text) noexcept { Assign(text ? std::string_view(text) : std::string_view{}); }

    CharBuf& operator=(std::string_view text) noexcept
    {
        Assign(text);
        return *this;
    }

    /** Present so `buf = "literal"` is not an ambiguity between the two converting constructors. */
    CharBuf& operator=(const char* text) noexcept
    {
        Assign(text ? std::string_view(text) : std::string_view{});
        return *this;
    }

    /** Contents up to the first NUL. Points into this buffer. */
    [[nodiscard]] std::string_view View() const noexcept
    {
        size_t length = 0;
        while (length < N && Value[length] != '\0')
            ++length;
        return {Value, length};
    }

    /** An owning copy of @ref View. */
    [[nodiscard]] std::string Str() const { return std::string(View()); }

    [[nodiscard]] bool Empty() const noexcept { return Value[0] == '\0'; }

    void Assign(std::string_view text) noexcept
    {
        const size_t length = std::min(text.size(), N - 1);
        std::memset(Value, 0, N);
        if (length > 0)
            std::memcpy(Value, text.data(), length);
    }
};

/**
 * @brief A property proxy for one schema field on one live entity.
 *
 * Declared as a member of an entity wrapper (`Field<int, "CBaseEntity", "m_iHealth"> Health{_e};`)
 * and used as if it were the field: it converts to T on read and takes a T on write. The offset
 * resolves once per (class, field) for the process; the proxy itself only carries the entity it
 * is bound to, so it is exactly as frame-local as its owner - never store one.
 *
 * A write to a networked field dirties it for replication automatically; that is the whole reason
 * to write through a Field rather than @ref WriteAt.
 *
 * Reading a field on a null entity, or one the schema does not have, yields `T{}`; writing it is
 * a no-op. Constness is the proxy's, not the entity's: a `const Pawn&` still writes, the same way
 * a `T* const` still writes through.
 *
 * @tparam ExpectedSize what the caller assumes is at the offset. Defaults to `sizeof(T)`, which
 *         is right whenever T *is* the field; pass 0 for a T read out of a larger wrapper field
 *         (a Vector at the head of a CNetworkViewOffsetVector) so the drift check stays quiet.
 */
template <class T, FixedString Klass, FixedString Name, size_t ExpectedSize = sizeof(T)>
class Field
{
public:
    explicit Field(CEntityInstance* owner) noexcept : _owner(owner) {}

    /** Copying a wrapper rebinds its fields to the same entity, so this copies the binding. */
    Field(const Field&) = default;

    /** Assignment copies the *value*, matching `a.Health = b.Health` reading as one. Rebinding is
     *  the wrapper's job, and entity wrappers are not assignable for exactly this reason. */
    const Field& operator=(const Field& other) const
    {
        Set(other.Get());
        return *this;
    }

    [[nodiscard]] T Get() const
    {
        const FieldRef& ref = Ref();
        if (!_owner || !ref)
            return T{};
        return ReadAt<T>(_owner, ref.Offset);
    }

    operator T() const { return Get(); }

    void Set(const T& value) const
    {
        const FieldRef& ref = Ref();
        if (!_owner || !ref)
            return;
        WriteAt<T>(_owner, ref.Offset, value);
        if (ref.Networked)
            MarkChanged(_owner, ref);
    }

    const Field& operator=(const T& value) const
    {
        Set(value);
        return *this;
    }

    const Field& operator+=(const T& value) const
        requires std::is_arithmetic_v<T>
    {
        Set(static_cast<T>(Get() + value));
        return *this;
    }

    const Field& operator|=(const T& value) const
        requires std::is_integral_v<T>
    {
        Set(static_cast<T>(Get() | value));
        return *this;
    }

    const Field& operator&=(const T& value) const
        requires std::is_integral_v<T>
    {
        Set(static_cast<T>(Get() & value));
        return *this;
    }

    /** Templated so `pawn.Team == TeamCT` picks this over the built-in comparison reached through
     *  `operator T()`; a plain `operator==(const T&)` ties with it whenever the literal's type
     *  differs from T. */
    template <class U>
    [[nodiscard]] bool operator==(const U& other) const
        requires std::equality_comparable_with<T, U>
    {
        return Get() == other;
    }

    template <class U>
    [[nodiscard]] auto operator<=>(const U& other) const
        requires std::three_way_comparable_with<T, U>
    {
        return Get() <=> other;
    }

    /** The resolved offset for this (class, field), shared by every Field of this type. */
    [[nodiscard]] static const FieldRef& Ref()
    {
        static const LazyField lazy{Klass.View(), Name.View(), ExpectedSize};
        return lazy.Ref();
    }

    /** The entity exists and the field resolved, so a read means something. Absent for a `bool`
     *  field, where it would collide with `operator T` - and `if (pawn.OnGroundLastTick)` reading
     *  as the value is what a caller wants there anyway. */
    explicit operator bool() const
        requires(!std::is_same_v<T, bool>)
    {
        return _owner != nullptr && static_cast<bool>(Ref());
    }

private:
    CEntityInstance* _owner;
};

}  // namespace VoltMod
