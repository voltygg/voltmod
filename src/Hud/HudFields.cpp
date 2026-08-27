#include "Hud/HudFields.hpp"

#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Entities/Field.hpp>
#include <cstdint>
#include <format>
#include <string>
#include <tier1/utlstring.h>
#include <tier1/utlvector.h>
#include <utility>

namespace VoltMod
{

// Resolved lazily because the schema system is not up until the first map load. Offsets come from
// the live schema, so a field that moves in a CS2 update needs nothing here; the sizes are the
// check that matters - they say the shape is still what this file reads, and a mismatch warns once
// at resolve time. Sizes from server.dll build 24934554.
static const LazyField kPanelIds{"CCSCustomHudLayout", "m_vecPanelIds", 24};
static const LazyField kClassNames{"CCSCustomHudLayout", "m_vecClassNames", 24};
static const LazyField kDialogVarNames{"CCSCustomHudLayout", "m_vecDialogVariableNames", 24};
static const LazyField kGlobalState{"CCSCustomHudLayout", "m_globalLayoutState", 408};
static const LazyField kPlayerStates{"CCSCustomHudLayout", "m_vecPlayerLayoutStates", 104};
static const LazyField kInputCapture{"CCSCustomHudLayoutState", "m_bInputCaptureEnabled", 1};

using StringTable = CUtlVector<CUtlString>;

/** EHudPanelClassStatus_t. Undefined defers to whatever the layout markup itself says. */
static constexpr int32_t kClassUndefined = -1;
static constexpr int32_t kClassAbsent = 0;
static constexpr int32_t kClassPresent = 1;

/** CS2 refuses to intern past this many entries per table, with a console warning. */
static constexpr int kTableCap = 1024;

/** Headroom kept below @ref kTableCap so a runaway caller cannot fill the tables permanently. */
static constexpr int kTableHeadroom = 64;

/** Count of one networked string table, or -1 when the field is unresolved. */
static int TableCount(CEntityInstance* entity, const LazyField& field)
{
    const FieldRef& ref = field.Ref();
    if (!entity || !ref)
        return -1;
    return MemberPtr<StringTable>(entity, ref.Offset)->Count();
}

/**
 * Fill @p out with @p text.
 *
 * `SetDirect` takes the view as it stands - it copies exactly that many bytes and terminates the
 * result itself - so nothing is allocated to NUL-terminate first. Assigned rather than returned by
 * value: `CUtlString`'s methods are tier0 imports, so the allocation happens inside tier0.dll, the
 * same allocator the engine frees with.
 */
static void SetStr(CUtlString& out, std::string_view text)
{
    out.SetDirect(text.data(), static_cast<int>(text.size()));
}

/**
 * The entity behind @p ref, or the reason it cannot be written to.
 *
 * @p slot may be @ref kEveryone, which skips the per-player range check.
 */
static Result<CEntityInstance*> ReadyForWrite(EntitySystem* entities, EntityRef ref, int slot)
{
    Entity entity = entities ? entities->Resolve(ref) : Entity{};
    if (!entity)
        return std::unexpected(Error::NotFound("the custom_hud_layout entity no longer exists"));

    // Interning past the engine's cap is refused game-side; stopping short keeps a caller in a
    // loop from filling the tables for the rest of the map.
    for (const auto& [name, field] : {std::pair{"panel id", &kPanelIds}, std::pair{"class name", &kClassNames},
                                      std::pair{"dialog variable", &kDialogVarNames}})
    {
        const int count = TableCount(entity.Raw(), *field);
        if (count >= kTableCap - kTableHeadroom)
            return std::unexpected(
                Error::Failed(std::format("the {} table is nearly full ({}/{})", name, count, kTableCap)));
    }

    if (slot == kEveryone)
        return entity.Raw();

    if (!IsValidSlot(slot))
        return std::unexpected(Error::Invalid(std::format("slot {} is not a player slot", slot)));

    // The engine's *ForPlayer setters compare the slot against m_vecPlayerLayoutStates and return
    // silently when it is out of range, so an empty vector would look exactly like success.
    const FieldRef& playerStates = kPlayerStates.Ref();
    if (!playerStates)
        return std::unexpected(Error::NotReady("the CustomHud per-player state field did not resolve"));

    const int states = ReadAt<int32_t>(entity.Raw(), playerStates.Offset);
    if (states <= slot)
        return std::unexpected(
            Error::Failed(std::format("slot {} has no per-player layout state (the entity holds {})", slot, states)));

    return entity.Raw();
}

/** Write @p className's state; @p state is one of the kClass* values. */
static Status WriteClassState(EntitySystem* entities, EntityRef ref, int slot, std::string_view panelId,
                              std::string_view className, int32_t state)
{
    auto entity = ReadyForWrite(entities, ref, slot);
    if (!entity)
        return std::unexpected(entity.error());

    const Bindings& bindings = entities->BindingsRef();
    CUtlString panel, name;
    SetStr(panel, panelId);
    SetStr(name, className);

    if (slot == kEveryone)
    {
        const auto& set = bindings.CustomHudSetHasClass;
        if (!set)
            return std::unexpected(Error::Unsupported("the CustomHud class setter did not bind"));
        set(*entity, &panel, &name, state);
        return {};
    }

    const auto& set = bindings.CustomHudSetHasClassForPlayer;
    if (!set)
        return std::unexpected(Error::Unsupported("the CustomHud per-player class setter did not bind"));
    set(*entity, slot, &panel, &name, state);
    return {};
}

int HudPlayerStateCount(EntitySystem* entities, EntityRef ref)
{
    Entity entity = entities ? entities->Resolve(ref) : Entity{};
    const FieldRef& states = kPlayerStates.Ref();
    if (!entity || !states)
        return -1;

    // CUtlVectorEmbeddedNetworkVar keeps its count first, which is exactly what the engine's own
    // IsInputCaptureEnabled reads before indexing.
    return ReadAt<int32_t>(entity.Raw(), states.Offset);
}

Status HudWriteText(EntitySystem* entities, EntityRef ref, int slot, std::string_view panelId,
                    std::string_view variable, std::string_view value)
{
    auto entity = ReadyForWrite(entities, ref, slot);
    if (!entity)
        return std::unexpected(entity.error());

    const Bindings& bindings = entities->BindingsRef();
    CUtlString panel, name, text;
    SetStr(panel, panelId);
    SetStr(name, variable);
    SetStr(text, value);

    if (slot == kEveryone)
    {
        const auto& set = bindings.CustomHudSetDialogVariable;
        if (!set)
            return std::unexpected(Error::Unsupported("the CustomHud dialog variable setter did not bind"));
        set(*entity, &panel, &name, &text);
        return {};
    }

    const auto& set = bindings.CustomHudSetDialogVariableForPlayer;
    if (!set)
        return std::unexpected(Error::Unsupported("the CustomHud per-player dialog variable setter did not bind"));
    set(*entity, slot, &panel, &name, &text);
    return {};
}

Status HudWriteClass(EntitySystem* entities, EntityRef ref, int slot, std::string_view panelId,
                     std::string_view className, bool on)
{
    return WriteClassState(entities, ref, slot, panelId, className, on ? kClassPresent : kClassAbsent);
}

Status HudResetClass(EntitySystem* entities, EntityRef ref, int slot, std::string_view panelId,
                     std::string_view className)
{
    return WriteClassState(entities, ref, slot, panelId, className, kClassUndefined);
}

Status HudWriteInputCapture(EntitySystem* entities, EntityRef ref, int slot, bool enabled)
{
    auto entity = ReadyForWrite(entities, ref, slot);
    if (!entity)
        return std::unexpected(entity.error());

    if (slot != kEveryone)
    {
        const auto& set = entities->BindingsRef().CustomHudSetInputCapture;
        if (!set)
            return std::unexpected(Error::Unsupported("the CustomHud input capture setter did not bind"));
        set(*entity, slot, enabled);
        return {};
    }

    // No engine setter takes the global state, so this one is written directly. It is safe to:
    // m_bInputCaptureEnabled is a plain bool inside an embedded struct, with no container and no
    // shadow hash index behind it - the reason every other write here goes through a setter.
    const FieldRef& state = kGlobalState.Ref();
    const FieldRef& capture = kInputCapture.Ref();
    if (!state || !capture)
        return std::unexpected(Error::NotReady("the CustomHud input capture field did not resolve"));

    const int32_t offset = state.Offset + capture.Offset;
    WriteAt<bool>(*entity, offset, enabled);

    // The chainer lives on the entity and the engine wants the absolute offset of what changed, so
    // the outer field's ref carries both with the summed offset substituted in.
    MarkChanged(
        *entity,
        FieldRef{
            .Offset = offset, .Size = capture.Size, .Networked = state.Networked, .ChainOffset = state.ChainOffset});
    return {};
}

Result<bool> HudReadInputCapture(EntitySystem* entities, EntityRef ref, int slot)
{
    Entity entity = entities ? entities->Resolve(ref) : Entity{};
    if (!entity)
        return std::unexpected(Error::NotFound("the custom_hud_layout entity no longer exists"));

    const auto& get = entities->BindingsRef().CustomHudIsInputCapture;
    if (!get)
        return std::unexpected(Error::Unsupported("the CustomHud input capture reader did not bind"));

    return get(entity.Raw(), slot);
}

}  // namespace VoltMod
