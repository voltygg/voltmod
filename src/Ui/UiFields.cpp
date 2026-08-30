#include "Ui/UiFields.hpp"

#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Schema/Api.hpp>
#include <cstdint>
#include <format>
#include <string>
#include <tier1/utlstring.h>
#include <tier1/utlvector.h>
#include <utility>

namespace VoltMod
{
using StringTable = CUtlVector<CUtlString>;

/** EHudPanelClassStatus_t. Undefined defers to whatever the layout markup itself says. */
static constexpr int32_t kClassUndefined = -1;
static constexpr int32_t kClassAbsent = 0;
static constexpr int32_t kClassPresent = 1;

/** CS2 refuses to intern past this many entries per table, with a console warning. */
static constexpr int kTableCap = 1024;

/** Headroom kept below @ref kTableCap so a runaway caller cannot fill the tables permanently. */
static constexpr int kTableHeadroom = 64;

/** Count of one networked string table, or -1 when the entity is gone.
 *
 *  The generated accessor bakes the vector's offset and returns its address; the element type
 *  stays here so no SDK container reaches a generated header. */
static int TableCount(void* table)
{
    return table ? static_cast<const StringTable*>(table)->Count() : -1;
}

/** Per-player layout state count, or -1 when the entity is gone. */
static int PlayerStateCount(const Schema::CCSCustomHudLayout& layout)
{
    // CUtlVectorEmbeddedNetworkVar keeps its count first, which is exactly what the engine's own
    // IsInputCaptureEnabled reads before indexing; the manifest reads that leading int32.
    return layout ? layout.PlayerLayoutStates() : -1;
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
    const Schema::CCSCustomHudLayout layout{entity.Raw()};
    for (const auto& [name, table] :
         {std::pair{"panel id", layout.PanelIds()}, std::pair{"class name", layout.ClassNames()},
          std::pair{"dialog variable", layout.DialogVariableNames()}})
    {
        const int count = TableCount(table);
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
    const int states = PlayerStateCount(layout);
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
            return std::unexpected(Error::Unsupported("the CustomUi class setter did not bind"));
        set(*entity, &panel, &name, state);
        return {};
    }

    const auto& set = bindings.CustomHudSetHasClassForPlayer;
    if (!set)
        return std::unexpected(Error::Unsupported("the CustomUi per-player class setter did not bind"));
    set(*entity, slot, &panel, &name, state);
    return {};
}

int UiPlayerStateCount(EntitySystem* entities, EntityRef ref)
{
    Entity entity = entities ? entities->Resolve(ref) : Entity{};
    return PlayerStateCount(Schema::CCSCustomHudLayout{entity.Raw()});
}

Status UiWriteText(EntitySystem* entities, EntityRef ref, int slot, std::string_view panelId, std::string_view variable,
                   std::string_view value)
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
            return std::unexpected(Error::Unsupported("the CustomUi dialog variable setter did not bind"));
        set(*entity, &panel, &name, &text);
        return {};
    }

    const auto& set = bindings.CustomHudSetDialogVariableForPlayer;
    if (!set)
        return std::unexpected(Error::Unsupported("the CustomUi per-player dialog variable setter did not bind"));
    set(*entity, slot, &panel, &name, &text);
    return {};
}

Status UiWriteClass(EntitySystem* entities, EntityRef ref, int slot, std::string_view panelId,
                    std::string_view className, bool on)
{
    return WriteClassState(entities, ref, slot, panelId, className, on ? kClassPresent : kClassAbsent);
}

Status UiResetClass(EntitySystem* entities, EntityRef ref, int slot, std::string_view panelId,
                    std::string_view className)
{
    return WriteClassState(entities, ref, slot, panelId, className, kClassUndefined);
}

Status UiWriteInputCapture(EntitySystem* entities, EntityRef ref, int slot, bool enabled)
{
    auto entity = ReadyForWrite(entities, ref, slot);
    if (!entity)
        return std::unexpected(entity.error());

    if (slot != kEveryone)
    {
        const auto& set = entities->BindingsRef().CustomHudSetInputCapture;
        if (!set)
            return std::unexpected(Error::Unsupported("the CustomUi input capture setter did not bind"));
        set(*entity, slot, enabled);
        return {};
    }

    // No engine setter takes the global state, so this one is written directly. It is safe to:
    // m_bInputCaptureEnabled is a plain bool inside an embedded struct, with no container and no
    // shadow hash index behind it - the reason every other write here goes through a setter.
    //
    // The embedded view carries the owning entity and its own offset within it, so the generated
    // setter dirties the entity at the summed offset without any of that being spelled out here.
    Schema::CCSCustomHudLayout{*entity}.GlobalLayoutState().SetInputCaptureEnabled(enabled);
    return {};
}

}  // namespace VoltMod
