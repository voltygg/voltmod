#pragma once

#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Entities/EntityOps.hpp>
#include <VoltMod/Entities/EntityRef.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Hud/HudClicks.hpp>
#include <functional>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief One player's view of a @ref Hud, from @ref Hud::For.
 *
 * The engine networks these writes through a single-slot recipient filter, so one entity really
 * can show different content to every player.
 *
 * Frame-local: use it in the expression that made it, like @ref Pawn. It holds no reference to
 * the @ref Hud, so it stays valid if that handle moves, but the entity behind it can still die.
 */
class HudPlayerView
{
public:
    HudPlayerView(const HudPlayerView&) = default;
    /** Views are not assignable; ask @ref Hud::For again. */
    HudPlayerView& operator=(const HudPlayerView&) = delete;

    /** Set the dialog variable a `text="{s:variable}"` attribute reads. */
    Status SetText(std::string_view panelId, std::string_view variable, std::string_view value);

    /** Add (@p on) or remove @p className on @p panelId. */
    Status SetClass(std::string_view panelId, std::string_view className, bool on);

    /** Hand @p className back to whatever the layout itself says. */
    Status ResetClass(std::string_view panelId, std::string_view className);

    /** Give this player a cursor, so the layout's Buttons can be hovered and clicked. */
    Status SetInputCapture(bool enabled);

    /** Whether this player currently has a cursor. */
    Result<bool> InputCaptureEnabled() const;

private:
    friend class Hud;

    /** @p entities may be null - a view onto an empty handle fails every call rather than
     *  crashing, the same as writing through the handle itself. */
    HudPlayerView(EntitySystem* entities, EntityRef ref, int slot) noexcept
        : _entities(entities), _ref(ref), _slot(slot)
    {}

    EntitySystem* _entities = nullptr;
    EntityRef _ref;
    int _slot = -1;
};

/**
 * @brief One spawned `custom_hud_layout`, owned.
 *
 * Unlike @ref Entity and @ref Pawn - frame-local wrappers you re-resolve - this handle *owns* its
 * entity: the destructor removes it, so a layout cannot outlive the plugin that spawned it across
 * a `meta reload`. Move-only. It stores an @ref EntityRef and resolves it on every call, so a map
 * change leaves the handle falsy and its destructor a no-op.
 *
 * Every write goes through the game's own setter rather than the netvar, because the tables they
 * append to are shadowed by server-only hash indexes that a direct write would leave stale. See
 * @ref custom_hud_guide for that and for what the client will render.
 *
 * Inert unless @ref Capability::CustomHud is on.
 */
class Hud
{
public:
    /** An empty handle: falsy, owns nothing, safe to destroy or assign over. */
    Hud() = default;

    /** Removes the entity if it still resolves. */
    ~Hud();

    Hud(Hud&& other) noexcept;
    /** Removes what this handle held before taking @p other's entity. */
    Hud& operator=(Hud&& other) noexcept;
    Hud(const Hud&) = delete;
    Hud& operator=(const Hud&) = delete;

    /** Whether the entity still exists. False after a map change or an explicit @ref Remove. */
    explicit operator bool() const;

    /** The entity behind this handle, for logging or comparing against a @ref HudClick. */
    [[nodiscard]] EntityRef Ref() const noexcept { return _ref; }

    /** Remove the entity now instead of at destruction. Idempotent. */
    void Remove();

    /** Set the dialog variable a `text="{s:variable}"` attribute reads, for everyone. */
    Status SetText(std::string_view panelId, std::string_view variable, std::string_view value);

    /** Add (@p on) or remove @p className on @p panelId, for everyone. */
    Status SetClass(std::string_view panelId, std::string_view className, bool on);

    /** Hand @p className back to whatever the layout itself says, for everyone. */
    Status ResetClass(std::string_view panelId, std::string_view className);

    /**
     * Give players a cursor, so the layout's Buttons can be hovered and clicked.
     *
     * Nothing in a layout is interactive without this: without capture the game keeps mouse-look
     * and the panel never sees a pointer.
     */
    Status SetInputCapture(bool enabled);

    /** Write one player's state instead of everyone's. */
    HudPlayerView For(int slot);

    /** How many per-player states the entity carries. Zero makes every @ref For call fail. */
    int PlayerStateCount() const;

    /**
     * Call @p handler when @p buttonId is pressed **in this layout**.
     *
     * Filtered by both, so two layouts sharing a button id do not trigger each other. Keep the
     * Subscription: dropping it unsubscribes, and the click hook is removed with the last one.
     *
     * A `Button` with no `id` attribute is dropped by the client before it is sent.
     */
    [[nodiscard]] Subscription OnClick(std::string buttonId, std::function<void(int slot)> handler);

    /** Every press in this layout, whichever Button it was. */
    [[nodiscard]] Subscription OnAnyClick(std::function<void(const HudClick&)> handler);

private:
    Hud(EntitySystem& entities, EntityOps& ops, Event<const HudClick&>& clicked, EntityRef ref) noexcept
        : _entities(&entities), _ops(&ops), _clicked(&clicked), _ref(ref)
    {}

    friend class CustomHud;

    EntitySystem* _entities = nullptr;
    EntityOps* _ops = nullptr;
    Event<const HudClick&>* _clicked = nullptr;
    EntityRef _ref;
};

/**
 * @brief Spawns `custom_hud_layout` entities and owns the hook their Buttons report through.
 *
 * Layouts are independent, so one plugin's HUD does not disturb another's. Inert unless
 * @ref Capability::CustomHud is on - the setters it calls are located by byte pattern and are
 * Windows-only today.
 */
class CustomHud
{
public:
    /** All must outlive this service; the Runtime declares them above it. */
    CustomHud(EntitySystem& entities, EntityOps& ops, const Bindings& bindings, Interfaces& interfaces,
              SlotEvents& slots)
        : Clicks(interfaces, bindings, slots), _entities(entities), _ops(ops)
    {}

    CustomHud(const CustomHud&) = delete;
    CustomHud& operator=(const CustomHud&) = delete;

    /**
     * Spawn a layout entity showing @p layout.
     *
     * @param layout a bare name (`"welcome"`), or a full resource name under
     *               `panorama/layout/custom_game/` with its **source** extension
     *               (`panorama/layout/custom_game/welcome.xml`, never `.vxml_c`). Anywhere else
     *               is refused: `gameinfo.gi`'s addon whitelist allows Panorama layouts in no
     *               other directory, and the client would reject it with nothing said server-side.
     * @return an owning handle. Errors when the name breaks that rule, when entity spawning is
     *         unavailable, or when the engine refuses to create the entity.
     */
    Result<Hud> Spawn(std::string_view layout);

    /**
     * Presses from **any** layout, including one another plugin spawned.
     *
     * @ref Hud::OnClick is the filtered form and what a plugin driving its own layout wants.
     * Subscribing to either installs the hook.
     */
    HudClicks Clicks;

private:
    EntitySystem& _entities;
    EntityOps& _ops;
};

}  // namespace VoltMod
