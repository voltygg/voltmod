#pragma once

#include <cstdint>
#include <string>

namespace VoltMod
{

/** @brief The "plugin" section of settings.jsonc. LoadStandardConfig applies the locale. */
struct StandardPluginSettings
{
    std::string locale = "en";
};

/** @brief The "menu" section of a plugin with a Panorama menu screen. */
struct PanoramaMenuSettings
{
    /** Draw the Panorama menu instead of center HTML; the client needs the compiled layout. */
    bool panorama = false;
    /** Workshop addon carrying the layout, required of every client; 0 requires nothing. */
    uint64_t addonId = 0;
};

}  // namespace VoltMod
