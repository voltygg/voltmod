#pragma once

#include <VoltMod/Core/Log.hpp>
#include <string>

namespace VoltMod
{

/**
 * @brief The default sink: tier0 `ConColorMsg`/`Msg`, one colour per level.
 *
 * The returned sink owns a copy of @p prefix and reaches the engine, so it may only be invoked on
 * the game thread - which is exactly the contract @ref VoltMod::Log::Sink already states.
 */
Log::Sink MakeConsoleSink(std::string prefix);

}  // namespace VoltMod
