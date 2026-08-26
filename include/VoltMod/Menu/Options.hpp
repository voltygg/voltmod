#pragma once

/**
 * @file Options.hpp
 * @brief Aggregate header pulling in every concrete @ref VoltMod::MenuOption type.
 *
 * Most consumers should include @ref VoltMod/Menu/MenuBuilder.hpp instead, which already
 * brings these in. Pull this header directly only when constructing options manually
 * (e.g. for `MenuBuilder::AddOption`) or when a custom subclass needs the base.
 */

#include <VoltMod/Menu/MenuOption.hpp>
#include <VoltMod/Menu/Options/ButtonOption.hpp>
#include <VoltMod/Menu/Options/ChoiceOption.hpp>
#include <VoltMod/Menu/Options/InputOption.hpp>
#include <VoltMod/Menu/Options/ProgressBarOption.hpp>
#include <VoltMod/Menu/Options/SelectorOption.hpp>
#include <VoltMod/Menu/Options/SliderOption.hpp>
#include <VoltMod/Menu/Options/SubmenuOption.hpp>
#include <VoltMod/Menu/Options/TextOption.hpp>
#include <VoltMod/Menu/Options/ToggleOption.hpp>
