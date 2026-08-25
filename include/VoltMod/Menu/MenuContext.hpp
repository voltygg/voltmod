#pragma once

#include <VoltMod/Core/Translations.hpp>
#include <string>
#include <string_view>

namespace VoltMod
{
class Runtime;
}

namespace VoltMod::Core
{
class EffectManager;
}

namespace VoltMod::Menu
{

/**
 * @brief Admin/target pair (plus the effect registry) that context-aware menu rows act on.
 *
 * Bind one per menu via @ref MenuBuilder::WithContext, then add rows with AddActionRow /
 * AddStateToggleRow / AddPresetChoiceRow / AddEffectToggleRow / AddEffectPickerRow - each row
 * derives its label (admin-language translation), its enabled state (permission + immunity via
 * runtime.Policy), and its dispatch target from here instead of per-row captures.
 */
struct MenuContext
{
    /** The live runtime, source of the roster, the policy and the translations. Declared first
     *  so a designated initializer can name it before Admin/Target. Null makes the context inert:
     *  @ref Tr echoes the key and @ref Allowed denies, so every row renders disabled. */
    Runtime* Rt = nullptr;
    int Admin = -1;
    int Target = -1;
    /** Required only by the effect rows; usually a member of the plugin's App. */
    Core::EffectManager* Effects = nullptr;

    /** Policy check: the admin holds @p permission and (when a distinct target is set) may
     *  act on them. Both players must still be connected. Empty permission skips that half.
     *  False when @ref Rt is null. */
    bool Allowed(const std::string& permission) const;

    /** Translate @p key in the admin's language, or return it unchanged when @ref Rt is null. */
    std::string Tr(std::string_view key, Core::Tokens tokens = {}) const;
};

}  // namespace VoltMod::Menu
