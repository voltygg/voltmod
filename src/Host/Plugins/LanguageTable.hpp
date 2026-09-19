#pragma once

#include <VoltMod/Core/Slots/PerSlot.hpp>
#include <VoltMod/Core/Slots/Slot.hpp>
#include <VoltMod/Host/IHostLanguages.hpp>
#include <string>
#include <string_view>

namespace VoltMod
{

/** The host's @ref IHostLanguages. @ref PluginHost resets a slot on connect and after disconnect. */
class LanguageTable final : public IHostLanguages
{
public:
    std::string_view Language(int slot) const override
    {
        return IsValidSlot(slot) ? std::string_view(_languages[slot]) : std::string_view{};
    }

    void SetLanguage(int slot, std::string_view lang) override
    {
        if (IsValidSlot(slot))
            _languages[slot] = lang;
    }

    void Reset(int slot) { _languages.Reset(slot); }

private:
    PerSlot<std::string> _languages;
};

}  // namespace VoltMod
