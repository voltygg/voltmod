#pragma once

#include <array>
#include <string_view>

namespace LabUi
{

inline constexpr std::string_view Layout = "lab";
inline constexpr std::string_view RootId = "lab";

// Panel ids.
inline constexpr std::string_view Icon = "lab_icon";
inline constexpr std::string_view Close = "lab_close";

// Dialog variables.
inline constexpr std::string_view CloseVar = "close";

// Repeated blocks: one struct per block, one array entry per index.
struct Row
{
    std::string_view Id;
    std::string_view Button;
    std::string_view Decrease;
    std::string_view Increase;
    std::string_view LabelVar;
    std::string_view HintVar;
    std::string_view ValueVar;
};
inline constexpr std::array<Row, 2> Rows{
    Row{"lab_row0", "lab_row0_button", "lab_row0_decrease", "lab_row0_increase", "row0_label", "row0_hint", "row0_value"},
    Row{"lab_row1", "lab_row1_button", "lab_row1_decrease", "lab_row1_increase", "row1_label", "row1_hint", "row1_value"},
};

inline constexpr std::array<std::string_view, 2> IconClasses{"Icon--ak47", "Icon--awp"};

}  // namespace LabUi
