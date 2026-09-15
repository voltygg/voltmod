#include "Core/GameBuild.hpp"

#include <VoltMod/Core/File.hpp>
#include <VoltMod/Core/Strings.hpp>
#include <string>

namespace VoltMod
{

std::string_view GameBuild()
{
    static const std::string build = [] {
        constexpr std::string_view key = "ServerVersion=";
        auto text = ReadAllText("steam.inf");
        const size_t at = text ? text->find(key) : std::string::npos;
        if (at == std::string::npos)
            return std::string("unknown");

        const size_t start = at + key.size();
        return Strings::Trim(std::string_view(*text).substr(start, text->find_first_of("\r\n", start) - start));
    }();
    return build;
}

}  // namespace VoltMod
