#pragma once

#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/Subscriptions.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Menu/MenuHost.hpp>
#include <VoltMod/Menu/MenuRow.hpp>
#include <VoltMod/Ui/UiPanel.hpp>
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/**
 * @brief The clickable @ref MenuHost: the same menus as @ref HtmlMenuManager, drawn into a
 * `custom_hud_layout` the player clicks.
 *
 * Rows carry their kind as a CSS class, so their look is the stylesheet's. Players get a cursor
 * for the session. Needs the layout on the client and @ref Capability::CustomUi and
 * @ref Capability::UiClicks; @ref HtmlMenuManager is the fallback. All entity writes happen from
 * the per-frame redraw.
 *
 * @see @ref custom_ui_guide for the id contract a replacement layout has to honour.
 */
class UiMenuManager final : public MenuHost
{
public:
    /** @p layout is the resource to drive (@ref DefaultLayout ships with the framework). All
     *  references must outlive the manager. Nothing is spawned until a menu opens. */
    UiMenuManager(Scheduler& scheduler, CustomUi& ui, SlotEvents& slots, EntitySystem& entities, ChatInput& chatInput,
                  Translations& translations, Policy& policy, PlayerManager& players,
                  std::string layout = std::string(DefaultLayout));
    ~UiMenuManager() override;

    /** The layout the framework ships, under `panorama/layout/custom_game/`. */
    static constexpr std::string_view DefaultLayout = "voltmod_menu";

    /** Rows one page shows. The layout has to declare exactly this many `vm_row{i}` runs. */
    static constexpr int RowsPerPage = 8;

    /** The layout resource currently driven. */
    [[nodiscard]] std::string_view Layout() const noexcept { return _panel.Name(); }

    /** Drive a different layout honouring the same ids. Closes every open menu first. */
    void SetLayout(std::string layout);

private:
    /** Panel ids written to and clicked from. A layout that omits one loses only that piece. */
    static constexpr std::string_view RootId = "vm_root";
    static constexpr std::string_view SubtitleId = "vm_subtitle";
    static constexpr std::string_view PagerId = "vm_pager";
    static constexpr std::string_view PrevId = "vm_prev";
    static constexpr std::string_view NextId = "vm_next";
    static constexpr std::string_view BackId = "vm_back";
    static constexpr std::string_view CloseId = "vm_close";
    static constexpr std::string_view PromptId = "vm_prompt";
    static constexpr std::string_view CancelId = "vm_cancel";
    static constexpr std::string_view RowPrefix = "vm_row";

    /** Dialog variables, all on @ref RootId: a `Label` resolves `{s:name}` through its ancestors,
     *  so the labels need no ids. Writing per label id does not work. */
    static constexpr std::string_view TitleVar = "vm_title";
    static constexpr std::string_view SubtitleVar = "vm_subtitle";
    static constexpr std::string_view PageVar = "vm_page";
    static constexpr std::string_view PromptVar = "vm_prompt_text";

    /** The class a row carries for its kind. */
    static std::string_view ModifierFor(MenuRowKind kind);

    /** Redraw open menus, hide closed ones. */
    void OnGameFrame();

    void Present(int slot) override;
    void Dismiss(int slot) override;

    /** Write the hidden state for @p slot: root collapsed, cursor released. */
    void Hide(int slot);

    void Bind(SlotEvents& slots);

    /** On the first draw, not in the constructor: a click subscription is refused until
     *  Runtime::Start has bound the hook. */
    void BindNav();

    void OnRowPressed(int slot, int row);
    void OnRowStepped(int slot, int row, int direction);
    void TurnPage(int slot, int delta);

    /** The item index row @p row of the player's current page stands for. */
    int ItemIndex(int slot, int row) const;

    UiPanel _panel;
    /** The row driver, held by pointer because the row id contract is internal to the framework. */
    std::unique_ptr<UiList> _rows;
    /** Slots whose entity state may show a menu: set by a draw, and by the slot changing hands
     *  because the new occupant inherits that state. */
    std::array<bool, MaxPlayers> _shown{};
    Subscriptions _subs;
    Subscriptions _nav;
    /** Declared last: the per-frame redraw drops before the state it touches. */
    Subscription _onFrame;
};

}  // namespace VoltMod
