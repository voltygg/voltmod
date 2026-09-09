// Rendered by `voltmod panorama render` from panorama/screens/lab.xml.j2. Do not edit.
#pragma once

#include <VoltMod/Ui/Widgets.hpp>
#include <array>
#include <string_view>

namespace LabUi
{

inline constexpr std::string_view Layout = "lab";
inline constexpr std::string_view RootId = "lab";

enum class Accent
{
    Good,
    Bad,
};
inline constexpr std::array<std::string_view, 2> AccentNames{"good", "bad"};
inline constexpr std::array<std::string_view, 2> AccentClasses{"Accent--good", "Accent--bad"};

enum class Icon
{
    Ak47,
    Awp,
};
inline constexpr std::array<std::string_view, 2> IconNames{"ak47", "awp"};
inline constexpr std::array<std::string_view, 2> IconClasses{"Icon--ak47", "Icon--awp"};

inline constexpr std::array<std::string_view, 5> StepClasses{
    "Step--0",
    "Step--1",
    "Step--2",
    "Step--3",
    "Step--4",
};

struct CardItem
{
    VoltMod::Flag Hidden;
    VoltMod::OneOf<2> Accent;
    VoltMod::OneOf<2> Icon;
    VoltMod::Text Title;
    VoltMod::Text Subtitle;
    VoltMod::Text Value;
    VoltMod::OneOf<5> Bar;
};

struct ToastPanel
{
    VoltMod::Flag Show;
    VoltMod::Text Title;
    VoltMod::Text Description;
    VoltMod::OneOf<2> Accent;
};

inline constexpr VoltMod::Flag Hidden{RootId, "Hidden"};

inline constexpr std::array<CardItem, 2> Card{{
    {
        {"lab_card0", "Hidden"},
        {"lab_card0_accent", AccentClasses},
        {"lab_card0_icon", IconClasses},
        {RootId, "card0_title"},
        {RootId, "card0_subtitle"},
        {RootId, "card0_value"},
        {"lab_card0_bar", StepClasses},
    },
    {
        {"lab_card1", "Hidden"},
        {"lab_card1_accent", AccentClasses},
        {"lab_card1_icon", IconClasses},
        {RootId, "card1_title"},
        {RootId, "card1_subtitle"},
        {RootId, "card1_value"},
        {"lab_card1_bar", StepClasses},
    },
}};

inline constexpr ToastPanel Toast{
    {"lab_toast", "Show"},
    {RootId, "toast_title"},
    {RootId, "toast_description"},
    {"lab_toast_accent", AccentClasses},
};

}  // namespace LabUi
