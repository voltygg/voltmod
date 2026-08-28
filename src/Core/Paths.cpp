#include <VoltMod/Core/Paths.hpp>
#include <format>

namespace VoltMod
{

static std::filesystem::path g_baseDir;

void SetBaseDir(const std::filesystem::path& baseDir)
{
    g_baseDir = baseDir;
}

std::filesystem::path ResolvePath(std::string_view relativePath)
{
    std::filesystem::path p(relativePath);
    return p.is_absolute() ? p : g_baseDir / p;
}

std::string AddonDir(std::string_view addon)
{
    return std::format("addons/{}", addon);
}

std::string AddonFile(std::string_view addon, std::string_view relative)
{
    return std::format("addons/{}/{}", addon, relative);
}

}  // namespace VoltMod
