#include "Menu/PanoramaIds.hpp"

#include <charconv>
#include <cstddef>

namespace VoltMod
{

static constexpr std::string_view kRowPrefix = "vm_row";

// The row index as the layout writes it: decimal digits and nothing else, so "01", "+1", "-1" and
// "1x" are all refused rather than rounded to a row a press could land on.
static bool ParseRowIndex(std::string_view text, int& row)
{
    if (text.empty() || (text.size() > 1 && text.front() == '0'))
        return false;

    for (char c : text)
    {
        if (c < '0' || c > '9')
            return false;
    }

    const char* end = text.data() + text.size();
    const auto [stop, error] = std::from_chars(text.data(), end, row);
    return error == std::errc{} && stop == end;
}

MenuPress ParseMenuButton(std::string_view id)
{
    if (id == "vm_back")
        return {.Button = MenuButton::Back};
    if (id == "vm_close")
        return {.Button = MenuButton::Close};
    if (id == "vm_prev")
        return {.Button = MenuButton::Prev};
    if (id == "vm_next")
        return {.Button = MenuButton::Next};
    if (id == "vm_cancel")
        return {.Button = MenuButton::Cancel};

    if (!id.starts_with(kRowPrefix))
        return {};

    std::string_view rest = id.substr(kRowPrefix.size());
    const std::size_t split = rest.rfind('_');
    if (split == std::string_view::npos)
        return {};

    const std::string_view suffix = rest.substr(split + 1);
    MenuButton button = MenuButton::None;
    if (suffix == "btn")
        button = MenuButton::Row;
    else if (suffix == "dec")
        button = MenuButton::RowDec;
    else if (suffix == "inc")
        button = MenuButton::RowInc;
    else
        return {};

    int row = -1;
    if (!ParseRowIndex(rest.substr(0, split), row))
        return {};

    return {.Button = button, .Row = row};
}

}  // namespace VoltMod
