#pragma once

#include <string_view>

namespace VoltMod
{

/** Each player's language, one table for every plugin. Crosses the host ABI through @ref IHost::Languages,
 *  so a change here bumps HostAbiVersion. Game thread only. */
class PlayerLanguages
{
public:
    /** Empty when no plugin has set one. */
    [[nodiscard]] virtual std::string_view Language(int slot) const = 0;

    /** Empty clears it. */
    virtual void SetLanguage(int slot, std::string_view lang) = 0;

protected:
    ~PlayerLanguages() = default;
};

}  // namespace VoltMod
