#pragma once

#include <VoltMod/Core/Result.hpp>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief Read @p path (resolved via ResolvePath) into a string.
 *
 * Binary mode: the bytes reach the caller exactly as written, so no text-mode line-ending
 * translation can alter an offset or split a UTF-8 sequence.
 *
 * @return ErrorCode::NotFound when the file cannot be opened.
 */
Result<std::string> ReadAllText(std::string_view path);

/**
 * @brief Write @p text to @p path (resolved via ResolvePath), creating its directory.
 *
 * Binary mode, so the file is byte-identical on every platform.
 */
Status WriteAllText(std::string_view path, std::string_view text);

}  // namespace VoltMod
