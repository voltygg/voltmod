#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Entities/CustomHud.hpp>
#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Entities/Field.hpp>
#include <VoltMod/Entities/KeyValues.hpp>
#include <cstdint>
#include <format>
#include <string>
#include <tier1/utlstring.h>
#include <tier1/utlvector.h>
#include <utility>

namespace VoltMod
{

// CCSCustomHudLayout's layout, resolved lazily because the schema system is not up until the
// first map load. Reads and verification only: writes go through the game's own setters, which
// also maintain the server-only hash indexes shadowing these tables.
static const LazyField kLayoutPath{"CCSCustomHudLayout", "m_strLayout"};
static const LazyField kPanelIds{"CCSCustomHudLayout", "m_vecPanelIds"};
static const LazyField kClassNames{"CCSCustomHudLayout", "m_vecClassNames"};
static const LazyField kDialogVarNames{"CCSCustomHudLayout", "m_vecDialogVariableNames"};
static const LazyField kGlobalState{"CCSCustomHudLayout", "m_globalLayoutState"};
static const LazyField kPlayerStates{"CCSCustomHudLayout", "m_vecPlayerLayoutStates"};
static const LazyField kInputCapture{"CCSCustomHudLayoutState", "m_bInputCaptureEnabled"};

using StringTable = CUtlVector<CUtlString>;

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
 * Constructed empty and assigned through `Set` rather than returned by value: `CUtlString`'s
 * methods are tier0 imports, so the allocation happens inside tier0.dll - the same allocator the
 * engine frees with - and nothing here depends on its copy constructor being exported.
 */
static void SetStr(CUtlString& out, std::string_view text)
{
    out.Set(std::string(text).c_str());
}

EntityRef HudLayout::Ref() const
{
    if (!_sys || !_e)
        return {};
    return Entity(*_sys, _e).Ref();
}

int HudLayout::PlayerStateCount() const
{
    const FieldRef& states = kPlayerStates.Ref();
    if (!_e || !states)
        return -1;

    // CUtlVectorEmbeddedNetworkVar keeps its count first, which is exactly what the engine's own
    // IsInputCaptureEnabled reads before indexing.
    return ReadAt<int32_t>(_e, states.Offset);
}

Result<CEntityInstance*> HudLayout::ReadyForWrite() const
{
    if (!_e || !_sys)
        return std::unexpected(Error::NotFound("the custom_hud_layout entity no longer exists"));

    // Interning past the engine's cap is refused game-side; stopping short keeps a caller in a
    // loop from filling the tables for the rest of the map.
    for (const auto& [name, field] : {std::pair{"panel id", &kPanelIds}, std::pair{"class name", &kClassNames},
                                      std::pair{"dialog variable", &kDialogVarNames}})
    {
        const int count = TableCount(_e, *field);
        if (count >= kTableCap - kTableHeadroom)
            return std::unexpected(
                Error::Failed(std::format("the {} table is nearly full ({}/{})", name, count, kTableCap)));
    }

    return _e;
}

Result<CEntityInstance*> HudLayout::ReadyForWrite(int slot) const
{
    auto entity = ReadyForWrite();
    if (!entity)
        return entity;

    if (!IsValidSlot(slot))
        return std::unexpected(Error::Invalid(std::format("slot {} is not a player slot", slot)));

    // The engine's *ForPlayer setters compare the slot against m_vecPlayerLayoutStates and return
    // silently when it is out of range, so an empty vector would look exactly like success.
    const int states = PlayerStateCount();
    if (states <= slot)
        return std::unexpected(Error::Failed(
            std::format("slot {} has no per-player layout state (the entity holds {})", slot, states)));

    return entity;
}

Status HudLayout::SetText(std::string_view panelId, std::string_view variable, std::string_view value)
{
    auto entity = ReadyForWrite();
    if (!entity)
        return std::unexpected(entity.error());

    const auto& set = _sys->BindingsRef().CustomHudSetDialogVariable;
    if (!set)
        return std::unexpected(Error::Unsupported("the CustomHud dialog variable setter did not bind"));

    CUtlString panel, name, text;
    SetStr(panel, panelId);
    SetStr(name, variable);
    SetStr(text, value);
    set(*entity, &panel, &name, &text);
    return {};
}

Status HudLayout::SetTextFor(int slot, std::string_view panelId, std::string_view variable, std::string_view value)
{
    auto entity = ReadyForWrite(slot);
    if (!entity)
        return std::unexpected(entity.error());

    const auto& set = _sys->BindingsRef().CustomHudSetDialogVariableForPlayer;
    if (!set)
        return std::unexpected(Error::Unsupported("the CustomHud per-player dialog variable setter did not bind"));

    CUtlString panel, name, text;
    SetStr(panel, panelId);
    SetStr(name, variable);
    SetStr(text, value);
    set(*entity, slot, &panel, &name, &text);
    return {};
}

Status HudLayout::SetClass(std::string_view panelId, std::string_view className, HudClass state)
{
    auto entity = ReadyForWrite();
    if (!entity)
        return std::unexpected(entity.error());

    const auto& set = _sys->BindingsRef().CustomHudSetHasClass;
    if (!set)
        return std::unexpected(Error::Unsupported("the CustomHud class setter did not bind"));

    CUtlString panel, name;
    SetStr(panel, panelId);
    SetStr(name, className);
    set(*entity, &panel, &name, static_cast<int32_t>(state));
    return {};
}

Status HudLayout::SetClassFor(int slot, std::string_view panelId, std::string_view className, HudClass state)
{
    auto entity = ReadyForWrite(slot);
    if (!entity)
        return std::unexpected(entity.error());

    const auto& set = _sys->BindingsRef().CustomHudSetHasClassForPlayer;
    if (!set)
        return std::unexpected(Error::Unsupported("the CustomHud per-player class setter did not bind"));

    CUtlString panel, name;
    SetStr(panel, panelId);
    SetStr(name, className);
    set(*entity, slot, &panel, &name, static_cast<int32_t>(state));
    return {};
}

Status HudLayout::SetInputCapture(bool enabled)
{
    auto entity = ReadyForWrite();
    if (!entity)
        return std::unexpected(entity.error());

    // No engine setter takes the global state, so this one is written directly. It is safe to:
    // m_bInputCaptureEnabled is a plain bool inside an embedded struct, with no container and no
    // shadow hash index behind it - the reason the rest of this file goes through the setters.
    const FieldRef& state = kGlobalState.Ref();
    const FieldRef& capture = kInputCapture.Ref();
    if (!state || !capture)
        return std::unexpected(Error::NotReady("the CustomHud input capture field did not resolve"));

    const int32_t offset = state.Offset + capture.Offset;
    WriteAt<bool>(*entity, offset, enabled);

    // The chainer lives on the entity and the engine wants the absolute offset of what changed,
    // so the outer field's ref carries both with the summed offset substituted in.
    MarkChanged(*entity, FieldRef{.Offset = offset,
                                  .Size = capture.Size,
                                  .Networked = state.Networked,
                                  .ChainOffset = state.ChainOffset});
    return {};
}

Status HudLayout::SetInputCaptureFor(int slot, bool enabled)
{
    auto entity = ReadyForWrite(slot);
    if (!entity)
        return std::unexpected(entity.error());

    const auto& set = _sys->BindingsRef().CustomHudSetInputCapture;
    if (!set)
        return std::unexpected(Error::Unsupported("the CustomHud input capture setter did not bind"));

    set(*entity, slot, enabled);
    return {};
}

Result<bool> HudLayout::InputCaptureEnabled(int slot) const
{
    if (!_e || !_sys)
        return std::unexpected(Error::NotFound("the custom_hud_layout entity no longer exists"));

    const auto& get = _sys->BindingsRef().CustomHudIsInputCapture;
    if (!get)
        return std::unexpected(Error::Unsupported("the CustomHud input capture reader did not bind"));

    return get(_e, slot);
}

std::vector<std::string> HudLayout::Describe() const
{
    std::vector<std::string> lines;

    // Offsets recovered from the shipped server.dll. Drift here means the bound setters are
    // addressing something else, so this is the first thing to check after a CS2 update.
    auto describe = [&lines](const char* name, const LazyField& field, int expected) {
        const FieldRef& ref = field.Ref();
        if (!ref)
        {
            lines.push_back(std::format("  {:<28} UNRESOLVED", name));
            return;
        }
        const char* verdict = ref.Offset == expected ? "" : "  <-- MOVED";
        lines.push_back(std::format("  {:<28} offset {:>5} (expect {:>5})  size {:>4}  net {}{}", name, ref.Offset,
                                    expected, ref.Size, ref.Networked ? "yes" : "no ", verdict));
    };

    lines.push_back("CCSCustomHudLayout");
    describe("m_strLayout", kLayoutPath, 0x4a8);
    describe("m_vecPlayerLayoutStates", kPlayerStates, 0x4b0);
    describe("m_globalLayoutState", kGlobalState, 0x518);
    describe("m_vecPanelIds", kPanelIds, 0x6b0);
    describe("m_vecClassNames", kClassNames, 0x6c8);
    describe("m_vecDialogVariableNames", kDialogVarNames, 0x6e0);
    lines.push_back("CCSCustomHudLayoutState");
    describe("m_bInputCaptureEnabled", kInputCapture, 0x34);

    if (!_e)
    {
        lines.push_back("entity: none");
        return lines;
    }

    if (const FieldRef& path = kLayoutPath.Ref())
    {
        const char* current = ReadAt<const char*>(_e, path.Offset);
        lines.push_back(std::format("layout resource: {}", current ? current : "(null)"));
    }

    lines.push_back(std::format("player states: {}", PlayerStateCount()));
    lines.push_back(std::format("tables: panelIds={} classNames={} dialogVariables={}", TableCount(_e, kPanelIds),
                                TableCount(_e, kClassNames), TableCount(_e, kDialogVarNames)));
    return lines;
}

Result<HudLayout> CustomHud::Spawn(std::string_view layoutResource)
{
    if (!_ops.CanSpawn())
        return std::unexpected(Error::Unsupported("entity spawning is unavailable"));
    if (layoutResource.empty())
        return std::unexpected(Error::Invalid("a layout resource name is required"));

    KeyValues kv;
    kv.Set("layout", std::string(layoutResource).c_str());

    CEntityInstance* entity = _ops.Spawn("custom_hud_layout", kv);
    if (!entity)
        return std::unexpected(Error::Engine("the engine refused to spawn custom_hud_layout"));

    return HudLayout(_entities, entity);
}

HudLayout CustomHud::Get(EntityRef ref)
{
    return HudLayout(_entities, _entities.Resolve(ref).Raw());
}

void CustomHud::Remove(EntityRef ref)
{
    if (Entity entity = _entities.Resolve(ref))
        _ops.Remove(entity.Raw());
}

}  // namespace VoltMod
