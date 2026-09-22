#pragma once

#include <VoltMod/Core/Slots/Slot.hpp>
#include <VoltMod/Core/Text/PlayerLanguages.hpp>
#include <array>
#include <string>
#include <string_view>

namespace VoltModTests
{

/** Stands in for the host's language table. */
class TestLanguages final : public VoltMod::PlayerLanguages
{
public:
    std::string_view Language(int slot) const override
    {
        return VoltMod::IsValidSlot(slot) ? std::string_view(Languages[slot]) : std::string_view{};
    }

    void SetLanguage(int slot, std::string_view lang) override
    {
        if (VoltMod::IsValidSlot(slot))
        {
            Languages[slot] = lang;
        }
    }

    std::array<std::string, VoltMod::MaxPlayers> Languages;
};

}  // namespace VoltModTests
