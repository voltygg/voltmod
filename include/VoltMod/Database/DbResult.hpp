#pragma once

#include <expected>
#include <string>

namespace VoltMod
{

/** Result of a database operation: the value on success, or an error message on failure. */
template <typename T>
using DbResult = std::expected<T, std::string>;

}  // namespace VoltMod
