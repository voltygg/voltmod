#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief Set the base directory for path resolution (typically from ISmmAPI::GetBaseDir()).
 * Must be called during initialization before any file loading.
 *
 * @param baseDir The base directory path to use for resolving relative paths.
 */
void SetBaseDir(const std::filesystem::path& baseDir);

/**
 * @brief  a relative path against the base directory.
 * If the path is already absolute, returns it as-is.
 * @param relativePath The relative path to resolve.
 * @return The resolved absolute path.
 */
std::filesystem::path ResolvePath(std::string_view relativePath);

/** @brief "addons/voltmod/plugins/<plugin>". Pure string building, so safe at static init. */
std::string PluginDir(std::string_view plugin);

/** @brief "addons/voltmod/plugins/<plugin>/<relative>". */
std::string PluginFile(std::string_view plugin, std::string_view relative);

}  // namespace VoltMod
