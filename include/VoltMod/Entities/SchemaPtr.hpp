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
 * static const FieldOffset kBody{"CBaseEntity", "m_CBodyComponent"};
 * static const FieldOffset kNode{"CBodyComponent", "m_pSceneNode", sizeof(void*)};
 * static const FieldOffset kOrigin{"CGameSceneNode", "m_vecAbsOrigin", sizeof(Vector)};
 *
 * SchemaPtr{entity}.SubObject(kBody).SubObject(kNode).Get<Vector>(kOrigin, Vector(0, 0, 0));
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

    /** The sub-object a pointer field points at (`m_pItemServices`), or null. */
    [[nodiscard]] SchemaPtr SubObject(const FieldOffset& field) const
    {
        void** pointer = Ptr<void*>(field);
        return pointer ? SchemaPtr{*pointer} : SchemaPtr{};
    }

    /** A sub-object stored inline (`m_modelState`): the same storage, further in. */
    [[nodiscard]] SchemaPtr Inside(const FieldOffset& field) const { return SchemaPtr{Ptr<uint8_t>(field)}; }

    /**
     * A pointer to the field itself, or nullptr. For a field read in place rather than copied out -
     * an engine container - and for one the engine declares as an array.
     */
    template <class T>
    [[nodiscard]] T* Ptr(const FieldOffset& field) const
    {
        const FieldRef& ref = field.Ref();
        if (!_base || !ref)
            return nullptr;
        return MemberPtr<T>(_base, ref.Offset);
    }

    /** The field's value, or @p fallback when this is null or the field did not resolve. */
    template <class T>
    [[nodiscard]] T Get(const FieldOffset& field, T fallback = T{}) const
    {
        const T* value = Ptr<const T>(field);
        return value ? *value : fallback;
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
    bool Set(const FieldOffset& field, const T& value) const
    {
        T* target = Ptr<T>(field);
        if (!target)
            return false;
        *target = value;
        return true;
    }

private:
    void* _base = nullptr;
};

}  // namespace VoltMod
