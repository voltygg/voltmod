#pragma once

#include <string_view>

namespace VoltMod
{

/** Each player's language, which @ref Translations reads and writes. Game thread only. */
class PlayerLanguages
{
public:
    [[nodiscard]] virtual std::string_view Language(int slot) const = 0;

    /** Empty clears it. */
    virtual void SetLanguage(int slot, std::string_view lang) = 0;

protected:
    ~PlayerLanguages() = default;
};

}  // namespace VoltMod
