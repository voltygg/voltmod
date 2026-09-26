#include "Loader/GameInfo.hpp"

#include <algorithm>
#include <cctype>

namespace VoltMod
{

static constexpr std::string_view Blank = " \t\r";

static bool SameKey(std::string_view left, std::string_view right)
{
    return std::ranges::equal(left, right, [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
}

/** Take the next bare or quoted word off the front of @p line. */
static std::string_view NextWord(std::string_view& line)
{
    const size_t start = line.find_first_not_of(Blank);
    if (start == std::string_view::npos)
    {
        line = {};
        return {};
    }
    line.remove_prefix(start);

    const bool quoted = line.front() == '"';
    const size_t end = quoted ? line.find('"', 1) : line.find_first_of(Blank);
    const std::string_view word = quoted ? line.substr(1, end - 1) : line.substr(0, end);
    line.remove_prefix(end == std::string_view::npos ? line.size() : end + (quoted ? 1 : 0));
    return word;
}

std::vector<std::string> GameSearchPaths(std::string_view gameinfo)
{
    std::vector<std::string> paths;
    bool inSearchPaths = false;
    int depth = 0;

    while (!gameinfo.empty())
    {
        const size_t lineEnd = gameinfo.find('\n');
        std::string_view line = gameinfo.substr(0, lineEnd);
        gameinfo.remove_prefix(lineEnd == std::string_view::npos ? gameinfo.size() : lineEnd + 1);
        line = line.substr(0, line.find("//"));

        const std::string_view key = NextWord(line);
        if (key.empty())
        {
            continue;
        }
        if (!inSearchPaths)
        {
            inSearchPaths = SameKey(key, "SearchPaths");
            continue;
        }

        if (key == "{")
        {
            ++depth;
        }
        else if (key == "}")
        {
            if (--depth == 0)
            {
                break;
            }
        }
        else if (depth == 1 && SameKey(key, "Game"))
        {
            paths.emplace_back(NextWord(line));
        }
    }
    return paths;
}

}  // namespace VoltMod
