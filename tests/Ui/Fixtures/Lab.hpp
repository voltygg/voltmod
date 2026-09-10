#pragma once

#include <array>
#include <string_view>

namespace LabUi
{

inline constexpr std::string_view Layout = "lab";
inline constexpr std::string_view RootId = "lab";

// Panel ids.
inline constexpr std::string_view Toast = "lab_toast";
inline constexpr std::string_view ToastAccent = "lab_toast_accent";

// Dialog variables, written through RootId.
inline constexpr std::string_view ToastTitleVar = "toast_title";
inline constexpr std::string_view ToastDescriptionVar = "toast_description";

// Repeated blocks: one struct per block, one array entry per index. Variables are
// written through RootId.
struct Card
{
    std::string_view Id;
    std::string_view Accent;
    std::string_view Icon;
    std::string_view Bar;
    std::string_view TitleVar;
    std::string_view SubtitleVar;
    std::string_view ValueVar;
};
inline constexpr std::array<Card, 2> Cards{
    Card{
        "lab_card0",
        "lab_card0_accent",
        "lab_card0_icon",
        "lab_card0_bar",
        "card0_title",
        "card0_subtitle",
        "card0_value",
    },
    Card{
        "lab_card1",
        "lab_card1_accent",
        "lab_card1_icon",
        "lab_card1_bar",
        "card1_title",
        "card1_subtitle",
        "card1_value",
    },
};

enum class Icon
{
    Ak47,
    Awp,
};
inline constexpr std::array<std::string_view, 2> IconNames{"ak47", "awp"};
inline constexpr std::array<std::string_view, 2> IconClasses{"Icon--ak47", "Icon--awp"};

enum class Accent
{
    Good,
    Bad,
};
inline constexpr std::array<std::string_view, 2> AccentNames{"good", "bad"};
inline constexpr std::array<std::string_view, 2> AccentClasses{"Accent--good", "Accent--bad"};

inline constexpr std::array<std::string_view, 5> StepClasses{
    "Step--0",
    "Step--1",
    "Step--2",
    "Step--3",
    "Step--4",
};

}  // namespace LabUi
