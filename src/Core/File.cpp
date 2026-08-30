#include <VoltMod/Core/File.hpp>
#include <VoltMod/Core/Paths.hpp>
#include <format>
#include <fstream>
#include <iterator>

namespace VoltMod
{

Result<std::string> ReadAllText(std::string_view path)
{
    const auto resolved = ResolvePath(path);
    std::ifstream file(resolved, std::ios::binary);
    if (!file.is_open())
        return std::unexpected(Error::NotFound(std::format("failed to open {}", resolved.string())));

    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

}  // namespace VoltMod
