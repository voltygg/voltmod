#pragma once

#include <VoltMod/Menu/MenuLayout.hpp>
#include <VoltMod/Menu/MenuModel.hpp>
#include <VoltMod/Ui/PlayerScreens.hpp>
#include <VoltMod/Ui/ScreenManager.hpp>
#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/**
 * @brief The @ref MenuLayout for a screen built from the `menu` Panorama block.
 *
 * @p screen is the layout name; @p tabs, @p rows and @p iconNames come from its generated header
 * (`Tabs.size()`, `Rows.size()`, `IconSetNames`). Each player gets their own screen, so the menu
 * survives death and spectating. @p screens and @p iconNames must outlive this; the generated
 * header's array does.
 */
class PanoramaMenuLayout final : public MenuLayout
{
public:
    PanoramaMenuLayout(ScreenManager& screens, std::string_view screen, std::size_t tabs, std::size_t rows,
                       std::span<const std::string_view> iconNames);

    int RowCount() const override { return static_cast<int>(_rows.size()); }
    int TabCount() const override { return static_cast<int>(_tabs.size()); }

    bool Show(int slot) override;
    void Hide(int slot) override;

    void SetHeader(int slot, const MenuHeader& header) override;
    void SetSidebarVisible(int slot, bool visible) override;
    void SetHomeVisible(int slot, bool visible) override;
    void SetTab(int slot, int index, const MenuTab* tab) override;
    void SetRow(int slot, int index, const MenuRow* row, std::string_view pendingHint) override;
    void SetEmpty(int slot, std::string_view text) override;
    void SetPager(int slot, std::string_view text) override;
    void SetPrompt(int slot, std::string_view text, std::string_view hint) override;
    void SetFooter(int slot, std::string_view back, std::string_view cancel) override;

    std::optional<MenuButton> ButtonFor(std::string_view id) const override;

    /** Fills `{s:<variable>}` in markup the screen adds itself, such as its `home` panel. Written
     *  with every header, so it outlives a respawned screen. */
    void AddText(std::string_view variable, std::function<std::string(int slot)> text);

private:
    struct TabIds
    {
        std::string Id;
        std::string Icon;
        std::string LabelVar;
    };

    struct RowIds
    {
        std::string Id;
        std::string Button;
        std::string Decrease;
        std::string Increase;
        std::string LabelVar;
        std::string HintVar;
        std::string ValueVar;
    };

    struct ScreenText
    {
        std::string Variable;
        std::function<std::string(int slot)> Value;
    };

    PlayerScreens _screens;
    std::string _root;
    std::string _subtitle;
    std::string _close;
    std::string _empty;
    std::string _prompt;
    std::string _cancel;
    std::string _back;
    std::string _page;
    std::string _pagePrevious;
    std::string _pageNext;
    std::vector<TabIds> _tabs;
    std::vector<RowIds> _rows;
    std::span<const std::string_view> _iconNames;
    std::vector<ScreenText> _texts;
};

}  // namespace VoltMod
