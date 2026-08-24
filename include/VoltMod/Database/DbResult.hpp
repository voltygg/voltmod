#pragma once

#include <VoltMod/Core/Log.hpp>
#include <expected>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace VoltMod::Database
{

/** Result of a database operation: the value on success, or an error message on failure. */
template <typename T>
using DbResult = std::expected<T, std::string>;

/**
 * Run a database call `fn` and turn a thrown std::exception into a logged error + an Err result,
 * instead of silently swallowing it. `fn` returns the success value `T`.
 *
 * SECURITY: `what` is a short caller label (e.g. the method name). NEVER pass a connection string,
 * DSN, or password into `what`, and keep secrets out of the queries `fn` runs - the exception text is
 * logged verbatim.
 */
template <typename Fn>
auto TryDb(std::string_view what, Fn&& fn) -> DbResult<std::invoke_result_t<Fn>>
{
    try
    {
        return fn();
    }
    catch (const std::exception& e)
    {
        Core::Log::Error("db: {} failed: {}", what, e.what());
        return std::unexpected(std::string(e.what()));
    }
}

/**
 * Like @ref TryDb but for call sites that keep a plain return type (std::optional / bool / int): run
 * `fn`, and on a thrown std::exception log it and return `fallback`. This replaces the silent
 * try/catch-return-default pattern so every failure leaves a log line. Same secret-safety rule as
 * @ref TryDb applies to `what`.
 */
template <typename T, typename Fn>
T TryOr(T fallback, std::string_view what, Fn&& fn)
{
    try
    {
        return fn();
    }
    catch (const std::exception& e)
    {
        Core::Log::Error("db: {} failed: {}", what, e.what());
        return fallback;
    }
}

}  // namespace VoltMod::Database
