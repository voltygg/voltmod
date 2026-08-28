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
#include <VoltMod/Ui/UiClicks.hpp>
#include <functional>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief One player's view of a @ref UiPanel, from @ref UiPanel::For: the same writes, networked
 * to a single slot. Frame-local, like @ref Pawn - use it in the expression that made it.
 */
class UiPlayerView
{
public:
    UiPlayerView(const UiPlayerView&) = default;
    /** Views are not assignable; ask @ref UiPanel::For again. */
    UiPlayerView& operator=(const UiPlayerView&) = delete;

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
    friend class UiPanel;

    /** @p entities may be null - a view onto an empty handle fails every call rather than
     *  crashing, the same as writing through the handle itself. */
    UiPlayerView(EntitySystem* entities, EntityRef ref, int slot) noexcept : _entities(entities), _ref(ref), _slot(slot)
    {}

    EntitySystem* _entities = nullptr;
    EntityRef _ref;
    int _slot = -1;
};

/**
 * @brief One spawned `custom_hud_layout`, owned: the destructor removes the entity, so a layout
 * cannot outlive the plugin that spawned it across a `meta reload`.
 *
 * Move-only. It stores an @ref EntityRef and resolves it on every call, so a map change leaves
 * the handle falsy and its destructor a no-op. Writes go through the game's own setters (see
 * @ref custom_ui_guide for why), and are inert unless @ref Capability::CustomUi is on.
 */
class UiPanel
{
public:
    /** An empty handle: falsy, owns nothing, safe to destroy or assign over. */
    UiPanel() = default;

    /** Removes the entity if it still resolves. */
    ~UiPanel();

    UiPanel(UiPanel&& other) noexcept;
    /** Removes what this handle held before taking @p other's entity. */
    UiPanel& operator=(UiPanel&& other) noexcept;
    UiPanel(const UiPanel&) = delete;
    UiPanel& operator=(const UiPanel&) = delete;

    /** Whether the entity still exists. False after a map change or an explicit @ref Remove. */
    explicit operator bool() const;

    /** The entity behind this handle, for logging or comparing against a @ref UiClick. */
    [[nodiscard]] EntityRef Ref() const noexcept { return _ref; }

    /** Remove the entity now instead of at destruction. Idempotent. */
    void Remove();

    /** Set the dialog variable a `text="{s:variable}"` attribute reads, for everyone. */
    Status SetText(std::string_view panelId, std::string_view variable, std::string_view value);

    /** Add (@p on) or remove @p className on @p panelId, for everyone. */
    Status SetClass(std::string_view panelId, std::string_view className, bool on);

    /** Hand @p className back to whatever the layout itself says, for everyone. */
    Status ResetClass(std::string_view panelId, std::string_view className);

    /** Give players a cursor. Nothing in a layout is clickable without this: the game keeps
     *  mouse-look and the panel never sees a pointer. */
    Status SetInputCapture(bool enabled);

    /** Write one player's state instead of everyone's. */
    UiPlayerView For(int slot);

    /** How many per-player states the entity carries. Zero makes every @ref For call fail. */
    int PlayerStateCount() const;

    /** Call @p handler when @p buttonId is pressed **in this layout** - filtered by both, so two
     *  layouts sharing a button id do not trigger each other. Keep the Subscription. */
    [[nodiscard]] Subscription OnClick(std::string buttonId, std::function<void(int slot)> handler);

    /** Every press in this layout, whichever Button it was. */
    [[nodiscard]] Subscription OnAnyClick(std::function<void(const UiClick&)> handler);

private:
    UiPanel(EntitySystem& entities, EntityOps& ops, Event<const UiClick&>& clicked, EntityRef ref) noexcept
        : _entities(&entities), _ops(&ops), _clicked(&clicked), _ref(ref)
    {}

    friend class CustomUi;

    EntitySystem* _entities = nullptr;
    EntityOps* _ops = nullptr;
    Event<const UiClick&>* _clicked = nullptr;
    EntityRef _ref;
};

/**
 * @brief Spawns `custom_hud_layout` entities and owns the hook their Buttons report through.
 * Layouts are independent, so one plugin's HUD does not disturb another's.
 */
class CustomUi
{
public:
    /** All must outlive this service; the Runtime declares them above it. */
    CustomUi(EntitySystem& entities, EntityOps& ops, const Bindings& bindings, Interfaces& interfaces,
             SlotEvents& slots, Scheduler& scheduler)
        : Clicks(interfaces, bindings, slots, entities, scheduler), _entities(entities), _ops(ops)
    {}

    CustomUi(const CustomUi&) = delete;
    CustomUi& operator=(const CustomUi&) = delete;

    /**
     * Spawn a layout entity showing @p layout: a bare name (`"welcome"`), or a full resource name
     * under `panorama/layout/custom_game/` with its **source** `.xml` extension - the one
     * directory the addon whitelist allows, and the client rejects anything else silently.
     * Errors when the name breaks that rule or the engine refuses the entity.
     */
    Result<UiPanel> Spawn(std::string_view layout);

    /** Presses from **any** layout, including one another plugin spawned. @ref UiPanel::OnClick
     *  is the filtered form most plugins want. Subscribing to either installs the hook. */
    UiClicks Clicks;

private:
    EntitySystem& _entities;
    EntityOps& _ops;
};

}  // namespace VoltMod
