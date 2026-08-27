#pragma once

#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Entities/Pawn.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/Options.hpp>
#include <VoltMod/Players/EffectDescriptor.hpp>
#include <VoltMod/Players/PlayerRef.hpp>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace VoltMod
{

// MenuManager is forward-declared in Engine/EngineTypes.hpp (reached transitively through
// Menu.hpp -> MenuOption.hpp): this header only stores a pointer and takes a reference
// parameter, never calling a method on it, so it does not need MenuManager's full definition
// (and the SDK-facing headers that pulls in). The .cpp files behind the context rows - which do
// call into it - include <VoltMod/Menu/MenuManager.hpp> themselves.

/**
 * @brief Fluent builder whose row methods append typed @ref MenuOption rows.
 *
 * The plain constructor builds rows that need no player context (Button, Toggle, Choice, ...).
 * Construct with a @ref MenuManager instead to also use the context rows (Row, StateToggle,
 * Presets, Effect, EffectPicker), which run their action/effect through the manager's long-lived
 * @ref ActionDispatcher - bind the admin/target pair once with @ref For:
 *
 * @code
 * MenuBuilder(runtime.Menus, "Admin Panel")
 *     .For(adminRef, targetRef, &app.Effects)
 *     .Row("action.kill", Actions::Kill)
 *     .StateToggle("action.freeze", InMoveType(MoveType::None), Actions::Freeze)
 *     .Presets("action.health", "HP", HealthPresets, Actions::SetHealth)
 *     .Effect(Effects::Ghost)
 *     .EffectPicker(Effects::Model)
 *     .Build();
 * @endcode
 */
class MenuBuilder
{
public:
    explicit MenuBuilder(const std::string& title) : _menu(std::make_shared<MenuView>()) { _menu->Title = title; }

    /** Also enables the context rows (@ref For, @ref Row, @ref StateToggle, @ref Presets,
     *  @ref Effect, @ref EffectPicker), dispatched through @p menus' long-lived ActionDispatcher. */
    MenuBuilder(MenuManager& menus, const std::string& title);

    /**
     * Test-only entry point: enables @ref Allowed and @ref For through @p policy directly, with no
     * MenuManager. Lets an SDK-free test exercise permission-gated rows against a fake Policy.
     * Context rows that dispatch (Row, StateToggle, Presets, Effect, EffectPicker) stay inert, as
     * they do for the plain constructor.
     */
    MenuBuilder(Policy& policy, const std::string& title) : _menu(std::make_shared<MenuView>()), _policy(&policy)
    {
        _menu->Title = title;
    }

    /**
     * Bind the admin/target pair (and optionally the effect registry) context rows act on.
     *
     * Context rows re-check `Policy::Authorize` for @p admin (and @p target, for a two-player
     * row) **when pressed**, against these references rather than whoever occupies their slots -
     * so a revoked permission, a departed admin and a reused target slot are all refused at
     * activation. The *enabled* state is a snapshot taken by @ref Allowed when the row is built,
     * and is not recomputed per redraw: a row can look enabled and still refuse. Rebuild the
     * menu to refresh it.
     *
     * Requires the @ref MenuManager constructor; a no-op otherwise.
     */
    MenuBuilder& For(PlayerRef admin, std::optional<PlayerRef> target, EffectManager* effects = nullptr)
    {
        _admin = admin;
        _target = target;
        _effects = effects;
        return *this;
    }

    /** Whether @p permission is granted for the bound admin/target (see @ref For). False - the
     *  row-disabled default - when no context is bound. */
    bool Allowed(std::string_view permission) const;

    /** Translate @p key in the bound admin's language, or return it unchanged when no context is
     *  bound. */
    std::string Tr(std::string_view key, Tokens tokens = {}) const;

    /** A button row that runs a single-target @ref Action against the bound admin/target. */
    MenuBuilder& Row(std::string_view labelKey, const Action& action);

    /**
     * A toggle row that re-evaluates @p isActive on every redraw and runs the
     * action when pressed. Predicates live in Entities/PawnPredicates.hpp.
     */
    MenuBuilder& StateToggle(std::string_view labelKey, std::function<bool(const Pawn&)> isActive,
                             const Action& action);

    /** Choice row: A/D selects a preset and E runs the action. */
    MenuBuilder& Presets(std::string_view labelKey, std::string_view unit, std::span<const int> presets,
                         const ParamAction& action, int initialIndex = 0);

    /** Effect toggle using the bound EffectManager (see @ref For) and reserved state labels. */
    MenuBuilder& Effect(const EffectDescriptor& effect);

    /** Effect-choice submenu (@ref EffectDescriptor::Choices), with a reset row when
     *  ResetLabelKey is set. */
    MenuBuilder& EffectPicker(const EffectDescriptor& effect);

    /** Append a non-selectable label row (heading or divider). */
    MenuBuilder& Text(const std::string& label)
    {
        _menu->Items.push_back(std::make_shared<TextOption>(label));
        return *this;
    }

    /** Append a plain action row. E fires the callback. */
    MenuBuilder& Button(const std::string& label, std::function<void(int)> onActivate, bool enabled = true)
    {
        _menu->Items.push_back(std::make_shared<ButtonOption>(label, std::move(onActivate), enabled));
        return *this;
    }

    /** Append an action row with a label that is recomputed every render. */
    MenuBuilder& DynamicButton(std::function<std::string()> getLabel, std::function<void(int)> onActivate,
                               bool enabled = true)
    {
        _menu->Items.push_back(std::make_shared<ButtonOption>(std::move(getLabel), std::move(onActivate), enabled));
        return *this;
    }

    /** Append a toggle row. E and A/D both flip. State is read via @p getState every frame. */
    MenuBuilder& Toggle(const std::string& title, const std::string& onLabel, const std::string& offLabel,
                        std::function<bool(int)> getState, std::function<void(int)> onToggle, bool enabled = true)
    {
        _menu->Items.push_back(std::make_shared<ToggleOption>(title, onLabel, offLabel, std::move(getState),
                                                              std::move(onToggle), enabled));
        return *this;
    }

    /** Append a string-labeled choice cycle. A/D walks the list; E commits the current value. */
    template <typename T>
    MenuBuilder& Choice(const std::string& title, std::vector<typename ChoiceOption<T>::Choice> choices,
                        std::function<int(int)> getIndex, std::function<void(int, int)> setIndex,
                        std::function<void(int, const T&)> onCommit = nullptr, bool enabled = true)
    {
        _menu->Items.push_back(std::make_shared<ChoiceOption<T>>(title, std::move(choices), std::move(getIndex),
                                                                 std::move(setIndex), std::move(onCommit), enabled));
        return *this;
    }

    /** Self-contained choice cycle: the option owns its index, no external get/set state. */
    template <typename T>
    MenuBuilder& Choice(const std::string& title, std::vector<typename ChoiceOption<T>::Choice> choices,
                        std::function<void(int, const T&)> onCommit, bool enabled = true, int initialIndex = 0)
    {
        _menu->Items.push_back(
            std::make_shared<ChoiceOption<T>>(title, std::move(choices), std::move(onCommit), enabled, initialIndex));
        return *this;
    }

    /** Like @ref Choice but uses a formatter to derive labels from arbitrary values. */
    template <typename T>
    MenuBuilder& Selector(const std::string& title, std::vector<T> values,
                          std::function<std::string(const T&)> formatter, std::function<int(int)> getIndex,
                          std::function<void(int, int)> setIndex, std::function<void(int, const T&)> onCommit = nullptr,
                          bool enabled = true)
    {
        _menu->Items.push_back(std::make_shared<SelectorOption<T>>(title, std::move(values), std::move(formatter),
                                                                   std::move(getIndex), std::move(setIndex),
                                                                   std::move(onCommit), enabled));
        return *this;
    }

    /** Append a numeric slider. A/D adjusts in `step` units, clamped to `[min, max]`. */
    MenuBuilder& Slider(const std::string& title, int min, int max, int step, std::function<int(int)> getValue,
                        std::function<void(int, int)> setValue, bool enabled = true)
    {
        _menu->Items.push_back(
            std::make_shared<SliderOption>(title, min, max, step, std::move(getValue), std::move(setValue), enabled));
        return *this;
    }

    /** Append a read-only progress bar. */
    MenuBuilder& ProgressBar(const std::string& title, std::function<int(int)> getValue, int max)
    {
        _menu->Items.push_back(std::make_shared<ProgressBarOption>(title, std::move(getValue), max));
        return *this;
    }

    /**
     * Append a free-text input row. E starts a chat capture; the player's next chat
     * line is routed to @p set. Return false from @p set to re-prompt for invalid input.
     */
    MenuBuilder& Input(const std::string& title, const std::string& prompt, std::function<std::string(int)> get,
                       std::function<bool(int, std::string_view)> set, int maxLength = 64, bool enabled = true)
    {
        _menu->Items.push_back(
            std::make_shared<InputOption>(title, prompt, std::move(get), std::move(set), maxLength, enabled));
        return *this;
    }

    /** Append a submenu link. E builds and pushes the submenu via @p factory. */
    MenuBuilder& Submenu(const std::string& label, std::function<std::shared_ptr<MenuView>(int)> factory,
                         bool enabled = true)
    {
        _menu->Items.push_back(std::make_shared<SubmenuOption>(label, std::move(factory), enabled));
        return *this;
    }

    /** Escape hatch: append a user-defined option subclass. */
    MenuBuilder& AddOption(std::shared_ptr<MenuOption> option)
    {
        _menu->Items.push_back(std::move(option));
        return *this;
    }

    /** Set a callback invoked with the player slot when the menu is dismissed. */
    MenuBuilder& OnClose(std::function<void(int)> callback)
    {
        _menu->OnClose = std::move(callback);
        return *this;
    }

    /** Override the default title + page-indicator header with custom HTML. */
    MenuBuilder& WithHeader(std::function<std::string()> header)
    {
        _menu->Layout.Header = std::move(header);
        return *this;
    }

    /** Override the default key-hints footer with custom HTML. */
    MenuBuilder& WithFooter(std::function<std::string()> footer)
    {
        _menu->Layout.Footer = std::move(footer);
        return *this;
    }

    /** Finalize and return the built menu. The builder must not be reused after this. */
    std::shared_ptr<MenuView> Build() { return std::move(_menu); }

private:
    std::shared_ptr<MenuView> _menu;
    /** Null for a context-free builder (see the plain constructor); every dispatching context row
     *  is then inert. Set by the @ref MenuManager constructor. */
    MenuManager* _menus = nullptr;
    /** Backs @ref Allowed. Set by either context constructor - the MenuManager one derives it from
     *  `menus.AccessPolicy()`, the test-only one takes it directly. Null makes @ref Allowed deny. */
    Policy* _policy = nullptr;
    PlayerRef _admin{};
    std::optional<PlayerRef> _target;
    EffectManager* _effects = nullptr;
};

}  // namespace VoltMod
