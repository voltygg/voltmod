#pragma once

#include <VoltMod/Engine/MemoryAccess.hpp>
#include <VoltMod/Entities/Field.hpp>
#include <cstdint>

namespace VoltMod
{

/**
 * @brief The base a schema offset is applied to: an entity, or a sub-object reached from one.
 *
 * Engine sub-objects (CPlayer_ObserverServices, CGameSceneNode) have no nameable C++ type, so the
 * code that reaches their fields would otherwise pass raw `void*` around and null-check by hand at
 * every step. A SchemaPtr is that pointer with the checks folded in: every accessor answers
 * harmlessly when the pointer is null or the field did not resolve, so a chain of reaches reads as
 * one expression.
 *
 * ```cpp
 * static const SchemaField<void> kBody{"CBaseEntity", "m_CBodyComponent"};
 * static const SchemaField<void*> kNode{"CBodyComponent", "m_pSceneNode"};
 * static const SchemaField<Vector> kOrigin{"CGameSceneNode", "m_vecAbsOrigin"};
 *
 * SchemaPtr{entity}.At(kBody).Follow(kNode).Get(kOrigin, Vector(0, 0, 0));
 * ```
 *
 * Use @ref Field instead for a field on an entity the framework wraps: a Field knows its owning
 * entity, so it can dirty a networked write for replication and a SchemaPtr cannot.
 *
 * It is a borrowed pointer with no lifetime of its own - as frame-local as the wrapper it came
 * from. Never store one. Game-thread only.
 */
class SchemaPtr
{
public:
    SchemaPtr() = default;

    /** Takes any engine pointer; a `CEntityInstance*` converts on its own. */
    explicit SchemaPtr(void* base) noexcept : _base(base) {}

    explicit operator bool() const noexcept { return _base != nullptr; }

    /** The bare pointer, for the engine calls and vtable hooks that take one. */
    [[nodiscard]] void* Raw() const noexcept { return _base; }

    /** Identity, which is what a hook comparing `this` pointers is asking about. */
    [[nodiscard]] bool operator==(const SchemaPtr&) const noexcept = default;

    /** Follow a schema pointer field (`m_pItemServices`), or return a null view. */
    template <class T>
    [[nodiscard]] SchemaPtr Follow(const SchemaField<T*>& field) const
    {
        return SchemaPtr{field.Get(_base, nullptr)};
    }

    /** Step into a sub-object stored inline (`m_modelState`). */
    template <class T>
    [[nodiscard]] SchemaPtr At(const SchemaField<T>& field) const
    {
        return SchemaPtr{field.Ptr(_base)};
    }

    /**
     * A pointer to the field itself, or nullptr. For a field read in place rather than copied out -
     * an engine container - and for one the engine declares as an array.
     */
    template <class T>
    [[nodiscard]] typename SchemaField<T>::Value* Ptr(const SchemaField<T>& field) const
    {
        return field.Ptr(_base);
    }

    /** The field's value, or @p fallback when this is null or the field did not resolve. */
    template <class T>
    [[nodiscard]] T Get(const SchemaField<T>& field, std::type_identity_t<T> fallback = T{}) const
        requires(!std::is_void_v<T> && !std::is_array_v<T>)
    {
        return field.Get(_base, fallback);
    }

    /**
     * Write the field.
     *
     * Nothing is dirtied for replication: a sub-object is not an entity, so there is no chain to
     * mark - what replicates is the owning entity's own pointer field, and the caller marks that
     * with @ref MarkChanged.
     *
     * @return false when this is null or the field did not resolve, and nothing was written.
     */
    template <class T>
    bool Set(const SchemaField<T>& field, const std::type_identity_t<T>& value) const
        requires(!std::is_void_v<T> && !std::is_array_v<T>)
    {
        return field.Set(_base, value);
    }

private:
    void* _base = nullptr;
};

}  // namespace VoltMod
