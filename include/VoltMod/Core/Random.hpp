#pragma once

#include <cstddef>

namespace VoltMod::Core
{

/**
 * A uniformly random index in `[0, count)`, or 0 when @p count is 0.
 *
 * The framework's single source of randomness, so features that need a random pick (`@random`
 * targeting, a random menu entry) do not each seed their own generator - or fall back to
 * something like the tick counter, which repeats within a frame.
 *
 * Game-thread only; the generator is not synchronised.
 */
std::size_t RandomIndex(std::size_t count);

}  // namespace VoltMod::Core
