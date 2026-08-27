#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Entities/EntityOps.hpp>
#include <VoltMod/Entities/EntityRef.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/** EHudPanelClassStatus_t: whether a panel carries a CSS class. */
enum class HudClass : int32_t
{
    Undefined = -1,  ///< leave the layout's own value alone
    Absent = 0,
    Present = 1,
};

/**
 * @brief One spawned `custom_hud_layout`, addressed by panel id.
 *
 * A **frame-local wrapper**, like @ref Entity: keep the @ref EntityRef from @ref Ref and resolve
 * it again through @ref CustomHud::Get, never this object. `explicit operator bool()` is the only
 * validity check.
 *
 * ### What the client renders
 *
 * CS2 networks the layout as a *resource name*, not as markup, so the client can only render a
 * layout it already has on disk - normally shipped in a workshop addon (see @ref Addons). The
 * name must sit under `panorama/layout/custom_game/`, which is the only Panorama layout path
 * `gameinfo.gi`'s addon whitelist allows, and the client validates the markup on arrival:
 * `Panel`, `Label`, `Image` and `Button` only, no `<scripts>` node, and every `Button` needs an
 * `id` attribute or its press is dropped. Rejections print on the *client* console.
 *
 * ### Why these are calls and not writes
 *
 * The three networked string tables the `*Index` fields point into are each shadowed by a
 * server-only `CUtlHashtable` that is neither in the schema nor networked, and the per-player
 * state keeps two more. Appending to a vector by hand leaves those indexes stale, so the next
 * engine-side call misses the hash, appends a duplicate, and the state silently desyncs. Every
 * write here therefore goes through the game's own setter, which interns, dedupes and notifies.
 *
 * ### Global and per-player
 *
 * Each method has a `...For(slot, ...)` counterpart writing one player's state, which the engine
 * networks through a single-slot recipient filter - one entity really can show different content
 * to every player. The per-player setters index `m_vecPlayerLayoutStates` and return silently
 * when the slot is past its end, so the `...For` methods check the count first and fail with a
 * reason rather than looking like they worked.
 */
class HudLayout
{
public:
    HudLayout() = default;

    /** @p entities must outlive this wrapper. Prefer @ref CustomHud::Get over building one. */
    HudLayout(EntitySystem& entities, CEntityInstance* raw) noexcept : _sys(&entities), _e(raw) {}

    HudLayout(const HudLayout&) = default;
    /** Wrappers are not assignable; re-resolve instead. See @ref Entity. */
    HudLayout& operator=(const HudLayout&) = delete;

    explicit operator bool() const noexcept { return _e != nullptr; }

    /** The entity this wraps. */
    [[nodiscard]] CEntityInstance* Raw() const noexcept { return _e; }

    /** The storable form of this layout. */
    [[nodiscard]] EntityRef Ref() const;

    /** Set the dialog variable a `text="{s:variable}"` attribute reads, for everyone. */
    Status SetText(std::string_view panelId, std::string_view variable, std::string_view value);
    /** @copydoc SetText. For one player. */
    Status SetTextFor(int slot, std::string_view panelId, std::string_view variable, std::string_view value);

    /** Add or remove a CSS class on @p panelId, for everyone. */
    Status SetClass(std::string_view panelId, std::string_view className, HudClass state);
    /** @copydoc SetClass. For one player. */
    Status SetClassFor(int slot, std::string_view panelId, std::string_view className, HudClass state);

    /**
     * Give players a cursor, so the layout's Buttons can be hovered and clicked.
     *
     * Nothing in a layout is interactive without this: without capture the game keeps mouse-look
     * and the panel never sees a pointer. Subscribe to `Hooks::HudClicks::Clicked` for the press.
     */
    Status SetInputCapture(bool enabled);
    /** @copydoc SetInputCapture. For one player. */
    Status SetInputCaptureFor(int slot, bool enabled);

    /** Whether @p slot currently has a cursor. */
    Result<bool> InputCaptureEnabled(int slot) const;

    /** How many per-player states the entity carries. Zero makes every `...For` call fail. */
    int PlayerStateCount() const;

    /**
     * One line per schema field - resolved offset, expected offset, size, whether it replicates -
     * plus the live string-table counts.
     *
     * This is the drift check after a CS2 update: an offset that has moved means the bound
     * setters are addressing something else, and the counts are how a repeated write is confirmed
     * to be idempotent rather than appending a duplicate.
     */
    std::vector<std::string> Describe() const;

private:
    /** The entity, or the reason it cannot be written to. */
    Result<CEntityInstance*> ReadyForWrite() const;
    /** ReadyForWrite plus the per-player range check. */
    Result<CEntityInstance*> ReadyForWrite(int slot) const;

    EntitySystem* _sys = nullptr;
    CEntityInstance* _e = nullptr;
};

/**
 * @brief Spawns and resolves `custom_hud_layout` entities.
 *
 * Owns nothing: a layout's lifetime is its entity's. Several may exist at once, and each is
 * independent, so one plugin's HUD does not disturb another's.
 *
 * Inert unless @ref Capability::CustomHud is on - the six setters it calls are located by byte
 * pattern and are Windows-only today.
 */
class CustomHud
{
public:
    /** All three must outlive this service. */
    CustomHud(EntitySystem& entities, EntityOps& ops, const Bindings& bindings)
        : _entities(entities), _ops(ops), _bindings(bindings)
    {}
    CustomHud(const CustomHud&) = delete;
    CustomHud& operator=(const CustomHud&) = delete;

    /**
     * Spawn a layout entity showing @p layoutResource.
     *
     * @param layoutResource resource name under `panorama/layout/custom_game/`, with its **source**
     *                       extension - `panorama/layout/custom_game/hud.xml`, not `.vxml_c`.
     * @return the new layout; store its @ref HudLayout::Ref. Errors when entity spawning is
     *         unavailable or the engine refuses to create the entity.
     */
    Result<HudLayout> Spawn(std::string_view layoutResource);

    /** Resolve a layout stored as a ref. Falsy when the entity is gone. */
    HudLayout Get(EntityRef ref);

    /** Remove the entity. No-op for a ref that no longer resolves. */
    void Remove(EntityRef ref);

private:
    EntitySystem& _entities;
    EntityOps& _ops;
    const Bindings& _bindings;
};

}  // namespace VoltMod
