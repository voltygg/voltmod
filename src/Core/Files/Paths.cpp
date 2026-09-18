#include <VoltMod/Core/Files/Paths.hpp>
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

std::string PluginDir(std::string_view plugin)
{
    return std::format("addons/voltmod/plugins/{}", plugin);
}

std::string PluginFile(std::string_view plugin, std::string_view relative)
{
    return std::format("{}/{}", PluginDir(plugin), relative);
}

}  // namespace VoltMod
