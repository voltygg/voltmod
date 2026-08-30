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

}  // namespace VoltMod
