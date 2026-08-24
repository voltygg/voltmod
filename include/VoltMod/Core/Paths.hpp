#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace VoltMod::Core
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
std::filesystem::path ResolvePath(const std::string& relativePath);

/** @brief "addons/<addon>" - the engine-relative install root the framework's loaders take.
 *  Pure string building, so safe at static init. */
std::string AddonDir(std::string_view addon);

/** @brief "addons/<addon>/<relative>", e.g. AddonFile("bhop", "configs/settings.jsonc"). */
std::string AddonFile(std::string_view addon, std::string_view relative);

}  // namespace VoltMod::Core
