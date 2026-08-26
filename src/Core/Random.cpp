#include <VoltMod/Core/Random.hpp>
#include <random>

namespace VoltMod::Core
{

std::size_t RandomIndex(std::size_t count)
{
    if (count == 0)
        return 0;

    // Seeded once per process; game code is single-threaded, so no synchronisation is needed.
    static std::mt19937 rng{std::random_device{}()};
    return std::uniform_int_distribution<std::size_t>(0, count - 1)(rng);
}

}  // namespace VoltMod::Core
