#pragma once

#include <array>
#include <string_view>

namespace LabUi
{

inline constexpr std::string_view Layout = "lab";
inline constexpr std::string_view RootId = "lab";

// Panel ids.
inline constexpr std::string_view Card0 = "lab_card0";
inline constexpr std::string_view Card0Accent = "lab_card0_accent";
inline constexpr std::string_view Card0Icon = "lab_card0_icon";
inline constexpr std::string_view Card0Bar = "lab_card0_bar";
inline constexpr std::string_view Card1 = "lab_card1";
inline constexpr std::string_view Card1Accent = "lab_card1_accent";
inline constexpr std::string_view Card1Icon = "lab_card1_icon";
inline constexpr std::string_view Card1Bar = "lab_card1_bar";
inline constexpr std::string_view Toast = "lab_toast";
inline constexpr std::string_view ToastAccent = "lab_toast_accent";

// Dialog variables, written through RootId.
inline constexpr std::string_view Card0TitleVar = "card0_title";
inline constexpr std::string_view Card0SubtitleVar = "card0_subtitle";
inline constexpr std::string_view Card0ValueVar = "card0_value";
inline constexpr std::string_view Card1TitleVar = "card1_title";
inline constexpr std::string_view Card1SubtitleVar = "card1_subtitle";
inline constexpr std::string_view Card1ValueVar = "card1_value";
inline constexpr std::string_view ToastTitleVar = "toast_title";
inline constexpr std::string_view ToastDescriptionVar = "toast_description";

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
