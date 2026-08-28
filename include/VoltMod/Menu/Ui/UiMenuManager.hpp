#pragma once

#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Menu/MenuHost.hpp>
#include <VoltMod/Menu/MenuRow.hpp>
#include <VoltMod/Ui/UiLayout.hpp>
#include <VoltMod/Ui/UiList.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/**
 * @brief Clickable Panorama menus, drawn into a `custom_hud_layout`.
 *
 * The other @ref MenuHost. Same menus as @ref HtmlMenuManager - a @ref MenuView built by
 * @ref MenuBuilder runs on either - drawn as a real panel the player clicks instead of a block of
 * center HTML they steer with WASD. Rows carry their kind as a CSS class, so how a toggle or a
 * submenu *looks* is the stylesheet's business and never the server's.
 *
 * There is no keyboard path here at all: presses arrive as clicks, which means the player needs a
 * cursor, which this turns on for the length of a menu session and off again when it ends.
 * Movement freeze still applies - a cursor takes mouse-look, not the movement keys.
 *
 * Costs that center HTML does not have: the layout has to reach clients (a workshop addon, or a
 * manual copy while developing) and @ref Capability::CustomUi has to be on, which today means
 * Windows. @ref HtmlMenuManager is the fallback when either is missing, and a plugin picks between
 * them once rather than switching per menu.
 *
 * @see @ref custom_ui_guide for the id contract a replacement layout has to honour.
 */
class UiMenuManager final : public MenuHost
{
public:
    /**
     * @param layout the layout resource to drive; the framework ships @ref DefaultLayout.
     * All references must outlive the manager. Nothing is spawned until a menu is opened, so
     * constructing one on a server that never shows a Panorama menu costs nothing.
     */
    UiMenuManager(Scheduler& scheduler, CustomUi& ui, SlotEvents& slots, EntitySystem& entities, ChatInput& chatInput,
                  Translations& translations, Policy& policy, PlayerManager& players,
                  std::string layout = std::string(DefaultLayout));

    /** The layout the framework ships, under `panorama/layout/custom_game/`. */
    static constexpr std::string_view DefaultLayout = "voltmod_menu";

    /** Rows one page shows. The layout has to declare exactly this many `vm_row{i}` runs. */
    static constexpr int RowsPerPage = 8;

    /** The layout resource currently driven. */
    [[nodiscard]] const std::string& Layout() const noexcept { return _layout.Name(); }

    /**
     * Drive a different layout honouring the same ids (see @ref custom_ui_guide).
     *
     * Closes every open menu first: the old entity is dropped, and a session left half-drawn
     * across two layouts is not worth the code to migrate.
     */
    void SetLayout(std::string layout);

private:
    /** Panel ids this driver writes. A layout that omits one loses that piece and nothing else. */
    static constexpr std::string_view RootId = "vm_root";
    static constexpr std::string_view TitleId = "vm_title";
    static constexpr std::string_view SubtitleId = "vm_subtitle";
    static constexpr std::string_view PagerId = "vm_pager";
    static constexpr std::string_view PageId = "vm_page";
    static constexpr std::string_view PrevId = "vm_prev";
    static constexpr std::string_view NextId = "vm_next";
    static constexpr std::string_view BackId = "vm_back";
    static constexpr std::string_view CloseId = "vm_close";
    static constexpr std::string_view PromptId = "vm_prompt";
    static constexpr std::string_view PromptTextId = "vm_prompt_text";
    static constexpr std::string_view CancelId = "vm_cancel";
    static constexpr std::string_view RowPrefix = "vm_row";

    /** The class a row carries for its kind, for a stylesheet to hang a chevron or a check on. */
    static std::string_view ModifierFor(MenuRowKind kind);

    /** Pages @p menu needs, never less than one so an empty menu still draws its chrome. */
    static int PageCount(const MenuView& menu);

    /** Per-tick redraw. Cheap because @ref UiLayout drops writes whose value the player has. */
    void OnGameFrame();

    void Present(int slot) override;
    void Dismiss(int slot) override;

    /** Subscribe the rows and the nav buttons, once. They follow @ref SetLayout on their own,
     *  because a @ref UiLayout keeps its identity when it changes resource. */
    void Bind();

    void OnRowPressed(int slot, int row);
    void OnRowStepped(int slot, int row, int direction);
    void TurnPage(int slot, int delta);

    /** The item index row @p row of the player's current page stands for. */
    int ItemIndex(int slot, int row) const;

    UiLayout _layout;
    UiList _rows;
    /** Row and nav subscriptions, held for the manager's lifetime. */
    std::vector<Subscription> _subs;
    /** Declared last: the frame pump drops before the state it touches. */
    Subscription _pump;
};

}  // namespace VoltMod
