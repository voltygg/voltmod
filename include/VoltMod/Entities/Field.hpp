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
 * @brief The resolved metadata for a schema field.
 *
 * A resolved value remains valid for the process. A negative offset means either the field is
 * missing or schema is not ready; compare its address with @ref PendingField to distinguish them.
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
 * FNV-1a 64 over `class::field`, used by the field cache and exposed for compatible tables.
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
 * Resolve @p field on @p klass, searching its base classes. The search does not descend into
 * derived classes.
 *
 * Hits and misses are cached for the process. A lookup made before schema and network metadata are
 * ready returns @ref PendingField without caching, so a later call can retry. If @p expectedSize
 * is non-zero, the first resolved lookup warns when the schema size differs. Game-thread only.
 *
 * @return A reference stable for the process lifetime.
 */
const FieldRef& ResolveField(std::string_view klass, std::string_view field, size_t expectedSize);

/**
 * The sentinel returned while schema is not ready. It and cached misses are both falsy.
 */
const FieldRef& PendingField() noexcept;

/**
 * Mark a networked field for the next snapshot. Does nothing for a null entity, unresolved field,
 * or non-networked field.
 */
void MarkChanged(CEntityInstance* entity, const FieldRef& ref);

/**
 * @brief A typed, lazily resolved schema field.
 *
 * Use with @ref SchemaPtr when no entity wrapper exposes the field. Use `T[]` for arrays and
 * `void` for untyped inline objects. Resolution retries until schema is ready. Game-thread only.
 *
 * ```cpp
 * static const SchemaField<void*> kItemServices{"CBasePlayerPawn", "m_pItemServices"};
 * static const SchemaField<int> kAccount{"CPlayer_ItemServices", "m_iAccount"};
 * int account = SchemaPtr{pawn}.Follow(kItemServices).Get(kAccount);
 * ```
 */
template <class T>
class SchemaField
{
public:
    using Value = std::remove_extent_t<T>;
    using StoredValue = std::conditional_t<std::is_void_v<Value>, std::byte, Value>;

    static consteval size_t DefaultExpectedSize()
    {
        if constexpr (std::is_void_v<T> || std::is_unbounded_array_v<T>)
            return 0;
        else
            return sizeof(T);
    }

    static constexpr size_t ExpectedSize = DefaultExpectedSize();

    /** @p klass and @p field must outlive this object. A zero @p expectedSize skips the size check. */
    constexpr SchemaField(std::string_view klass, std::string_view field, size_t expectedSize = ExpectedSize) noexcept
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

    /** The field address, or null when @p base or the field is unavailable. */
    [[nodiscard]] Value* Ptr(void* base) const
    {
        const FieldRef& ref = Ref();
        return base && ref ? MemberPtr<Value>(base, ref.Offset) : nullptr;
    }

    /** The field value, or @p fallback. Arrays and untyped inline objects are pointer-only. */
    [[nodiscard]] StoredValue Get(void* base, StoredValue fallback = StoredValue{}) const
        requires(!std::is_void_v<T> && !std::is_array_v<T>)
    {
        const auto* value = Ptr(base);
        return value ? *value : fallback;
    }

    /** Write the field, returning false when @p base or the field is unavailable. */
    bool Set(void* base, const StoredValue& value) const
        requires(!std::is_void_v<T> && !std::is_array_v<T>)
    {
        Value* target = Ptr(base);
        if (!target)
            return false;
        *target = value;
        return true;
    }

private:
    std::string_view _klass;
    std::string_view _field;
    size_t _expectedSize;
    mutable const FieldRef* _ref = nullptr;
};

template <size_t N>
struct FixedString
{
    char Value[N]{};

    constexpr FixedString(const char (&text)[N]) { std::copy_n(text, N, Value); }

    [[nodiscard]] constexpr std::string_view View() const { return {Value, N - 1}; }
};

/**
 * @brief A value for a fixed engine character array (`char m_name[N]`).
 *
 * Assignment truncates to N-1 characters, adds a NUL, and clears the unused tail. @ref View
 * borrows this buffer; @ref Str returns a copy.
 */
template <size_t N>
struct CharBuf
{
    static_assert(N > 1, "a CharBuf needs room for at least one character and a NUL");

    char Value[N]{};

    CharBuf() = default;
    CharBuf(std::string_view text) noexcept { Assign(text); }

    /** Supports direct construction from a literal without two user-defined conversions. */
    CharBuf(const char* text) noexcept { Assign(text ? std::string_view(text) : std::string_view{}); }

    CharBuf& operator=(std::string_view text) noexcept
    {
        Assign(text);
        return *this;
    }

    /** Avoids ambiguous assignment from a string literal. */
    CharBuf& operator=(const char* text) noexcept
    {
        Assign(text ? std::string_view(text) : std::string_view{});
        return *this;
    }

    /** Contents before the first NUL, borrowed from this buffer. */
    [[nodiscard]] std::string_view View() const noexcept
    {
        size_t length = 0;
        while (length < N && Value[length] != '\0')
            ++length;
        return {Value, length};
    }

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
 * @brief A schema-field proxy bound to a live entity.
 *
 * Entity wrappers declare these as members and use them like values. Each specialization shares
 * one resolved @ref SchemaField and each proxy stores only its entity pointer. The proxy is
 * frame-local and must not be stored.
 *
 * Writes mark networked fields for replication. Reads from a null entity or missing field return
 * `T{}`; writes do nothing. A const proxy can still modify the entity, like a const pointer value.
 *
 * @tparam ExpectedSize Expected schema size. Pass 0 when T reads only the leading value of a
 *         larger field.
 */
template <class T, FixedString Klass, FixedString Name, size_t ExpectedSize = sizeof(T)>
class Field
{
public:
    explicit Field(CEntityInstance* owner) noexcept : _owner(owner) {}

    Field(const Field&) = default;

    [[nodiscard]] T Get() const { return _schema.Get(_owner); }

    operator T() const { return Get(); }

    void Set(const T& value) const
    {
        if (!_schema.Set(_owner, value))
            return;
        const FieldRef& ref = Ref();
        if (ref.Networked)
            MarkChanged(_owner, ref);
    }

    const Field& operator=(const T& value) const
    {
        Set(value);
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

    /** Prevents differently typed comparisons from tying with the conversion to T. */
    template <class U>
    [[nodiscard]] bool operator==(const U& other) const
        requires std::equality_comparable_with<T, U>
    {
        return Get() == other;
    }

    [[nodiscard]] static const FieldRef& Ref() { return _schema.Ref(); }

private:
    /** Constant-initialized to avoid a function-local static guard on field access. */
    static inline SchemaField<T> _schema{Klass.View(), Name.View(), ExpectedSize};

    CEntityInstance* _owner;
};

}  // namespace VoltMod
