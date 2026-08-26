#pragma once

#include <VoltMod/Core/EffectManager.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Runtime.hpp>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief Admin/target pair (plus the effect registry) that context-aware menu rows act on.
 *
 * Bind one per menu via @ref MenuBuilder::WithContext, then add rows with AddActionRow /
 * AddStateToggleRow / AddPresetChoiceRow / AddEffectToggleRow / AddEffectPickerRow - each row
 * derives its label (admin-language translation), its enabled state (`runtime.Policy.Authorize`),
 * and its dispatch target from here instead of per-row captures.
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
    EffectManager* Effects = nullptr;

    /** Whether this row may be used: `runtime.Policy.Authorize` for the admin, the target (when
     *  one is set) and @p permission. Rendering a row is a permission question like any other,
     *  so it asks the one gate rather than repeating its rules. False when @ref Rt is null. */
    bool Allowed(std::string_view permission) const;

    /** Translate @p key in the admin's language, or return it unchanged when @ref Rt is null. */
    std::string Tr(std::string_view key, Tokens tokens = {}) const;
};

}  // namespace VoltMod
