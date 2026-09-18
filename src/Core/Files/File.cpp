#include <VoltMod/Core/Files/File.hpp>
#include <VoltMod/Core/Files/Paths.hpp>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>

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

Status WriteAllText(std::string_view path, std::string_view text)
{
    const auto resolved = ResolvePath(path);
    std::error_code ignored;
    std::filesystem::create_directories(resolved.parent_path(), ignored);

    std::ofstream file(resolved, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return std::unexpected(Error::Invalid(std::format("failed to open {}", resolved.string())));

    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file)
        return std::unexpected(Error::Invalid(std::format("failed to write {}", resolved.string())));
    return {};
}

}  // namespace VoltMod
