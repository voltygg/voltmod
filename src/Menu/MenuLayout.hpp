#pragma once

#include <VoltMod/Menu/Menu.hpp>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief Where a menu screen keeps its parts, so the renderer never spells an id.
 *
 * Ids are the full panel ids the layout carries; the `*Var` names are dialog variables on
 * @ref Root. @ref DefaultMenuLayout describes the screen the framework ships.
 */
struct MenuLayoutSpec
{
    std::string_view Layout;     ///< Layout resource name, what the panel is spawned with.
    std::string_view Root;       ///< Root panel id, the one every dialog variable resolves through.
    std::string_view RowPrefix;  ///< Row ids are this plus the row index.
    int RowsPerPage;

    std::string_view SubtitleId;
    std::string_view PagerId;
    std::string_view PrevId;
    std::string_view NextId;
    std::string_view BackId;
    std::string_view CloseId;
    std::string_view PromptId;
    std::string_view CancelId;

    std::string_view TitleVar;
    std::string_view SubtitleVar;
    std::string_view BreadcrumbVar;
    std::string_view PageVar;
    std::string_view PromptTextVar;
    std::string_view PromptHintVar;
    std::string_view NavBackVar;
    std::string_view NavCloseVar;
    std::string_view NavCancelVar;

    /// Zero when the screen has no nav strip; otherwise ids are @ref NavPrefix plus the index.
    int NavCount;
    std::string_view NavPrefix;
};

/** The screen shipped as `panorama/screens/voltmod_menu.xml.j2`, with its full row and tab pool. */
inline constexpr MenuLayoutSpec DefaultMenuLayout{
    .Layout = "voltmod_menu",
    .Root = "voltmod_menu",
    .RowPrefix = "voltmod_menu_row",
    .RowsPerPage = 10,
    .SubtitleId = "voltmod_menu_subtitle",
    .PagerId = "voltmod_menu_pager",
    .PrevId = "voltmod_menu_prev",
    .NextId = "voltmod_menu_next",
    .BackId = "voltmod_menu_back",
    .CloseId = "voltmod_menu_close",
    .PromptId = "voltmod_menu_prompt",
    .CancelId = "voltmod_menu_cancel",
    .TitleVar = "title",
    .SubtitleVar = "subtitle",
    .BreadcrumbVar = "breadcrumb",
    .PageVar = "page",
    .PromptTextVar = "prompt_text",
    .PromptHintVar = "prompt_hint",
    .NavBackVar = "back",
    .NavCloseVar = "close",
    .NavCancelVar = "cancel",
    .NavCount = 8,
    .NavPrefix = "voltmod_menu_nav",
};

/** The ids and dialog variables one row of a menu screen is written through. */
struct MenuRowIds
{
    std::string Row;       ///< Row panel id: classes.
    std::string Button;    ///< The row's own Button, what a press on the row reports.
    std::string Dec;       ///< Step down.
    std::string Inc;       ///< Step up.
    std::string LabelVar;  ///< Dialog variable on @ref MenuLayoutSpec::Root.
    std::string ValueVar;  ///< Dialog variable on @ref MenuLayoutSpec::Root.
};

/** Row @p row of @p spec, ids and variables. Out-of-range rows are not checked; the host pages. */
[[nodiscard]] MenuRowIds RowIds(const MenuLayoutSpec& spec, int row);

/** Panel id of nav button @p index, empty when the screen has no nav strip. */
[[nodiscard]] std::string NavId(const MenuLayoutSpec& spec, int index);

/** Dialog variable holding nav button @p index's label. */
[[nodiscard]] std::string NavVar(const MenuLayoutSpec& spec, int index);

/** What a Button in a menu layout stands for. */
enum class MenuButton
{
    None,    ///< Not one of the menu's ids.
    Row,     ///< `<row>_btn`: activate the row.
    RowDec,  ///< `<row>_dec`: step the row's value down.
    RowInc,  ///< `<row>_inc`: step the row's value up.
    Nav,     ///< A nav strip tab.
    Back,
    Close,
    Prev,
    Next,
    Cancel  ///< Drop the chat prompt.
};

/** One press, as the layout named it. */
struct MenuPress
{
    MenuButton Button = MenuButton::None;
    /** Row of the current page for a row press, the tab for @ref MenuButton::Nav, else -1. The
     *  index is whatever the id spelled: the host checks it against the spec's counts. */
    int Row = -1;
};

/**
 * A menu layout's Button ids, parsed against @p spec.
 *
 * One handler over @ref CustomUi::Clicked reads every press, so the id contract lives here rather
 * than in a subscription per id. The text is client-controlled, so anything that is not exactly
 * one of the ids - a wrong prefix, an index with a leading zero or a sign, a suffix the layout
 * does not declare - is @ref MenuButton::None rather than a guess. SDK-free, so the rules are
 * unit-tested.
 */
[[nodiscard]] MenuPress ParseMenuButton(const MenuLayoutSpec& spec, std::string_view id);

/**
 * The class vocabulary a menu's stylesheet styles against.
 *
 * Every one of them is written on every draw, on or off, so a row never keeps a class the state it
 * stood for has left; @ref UiPanel's write cache makes the ones that did not change free.
 */
struct MenuCss
{
    static constexpr std::string_view Hidden = "Hidden";
    static constexpr std::string_view Disabled = "Disabled";
    static constexpr std::string_view Selected = "Selected";
    static constexpr std::string_view Changed = "Changed";
    static constexpr std::string_view Pending = "Pending";
    static constexpr std::string_view HasValue = "HasValue";
    static constexpr std::string_view HasSteppers = "HasSteppers";
    static constexpr std::string_view On = "On";
    static constexpr std::string_view Prompting = "Prompting";
    static constexpr std::string_view KeyHints = "KeyHints";
    static constexpr std::string_view Root = "Root";
};

/** The `Kind--*` class for @p kind. */
[[nodiscard]] std::string_view MenuKindClass(MenuRowKind kind);

}  // namespace VoltMod
