#pragma once

#include <CS2Kit/Core/CoreServices.hpp>
#include <CS2Kit/Utils/Translations.hpp>
#include <string>
#include <string_view>

namespace CS2Kit::Core
{
class EffectManager;
}

namespace CS2Kit::Menu
{

/**
 * @brief Admin/target pair (plus the effect registry) that context-aware menu rows act on.
 *
 * Bind one per menu via @ref MenuBuilder::WithContext, then add rows with AddActionRow /
 * AddStateToggleRow / AddPresetChoiceRow / AddEffectToggleRow / AddEffectPickerRow - each row
 * derives its label (admin-language translation), its enabled state (permission + immunity via
 * Core::Ctx().Policy), and its dispatch target from here instead of per-row captures.
 */
struct MenuContext
{
    int Admin = -1;
    int Target = -1;
    /** Required only by the effect rows; usually `&App().Effects`. */
    Core::EffectManager* Effects = nullptr;

    /** Policy check: the admin holds @p permission and (when a distinct target is set) may
     *  act on them. Both players must still be connected. Empty permission skips that half. */
    bool Allowed(const std::string& permission) const;

    /** Translate @p key in the admin's language. */
    std::string Tr(std::string_view key, Utils::Tokens tokens = {}) const;
};

}  // namespace CS2Kit::Menu
