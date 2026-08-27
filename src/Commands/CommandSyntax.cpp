#include "Commands/CommandSyntax.hpp"

#include <array>

namespace VoltMod::CommandSyntax
{

/** The chat prefixes, in the order they are matched. The first is what a usage line shows. */
static const std::array<std::string_view, 2> kPrefixes{"!", "."};

std::string_view ChatPrefix()
{
    return kPrefixes.front();
}

std::vector<std::string> Tokenize(std::string_view text)
{
    std::vector<std::string> tokens;
    std::string current;
    bool quoted = false;
    bool started = false;  // a `""` is an argument, even though the token is empty

    for (size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if (c == '\\' && i + 1 < text.size() && text[i + 1] == '"')
        {
            current += '"';
            ++i;
            started = true;
            continue;
        }
        if (c == '"')
        {
            quoted = !quoted;
            started = true;
            continue;
        }
        if (c == ' ' && !quoted)
        {
            if (started || !current.empty())
                tokens.push_back(current);
            current.clear();
            started = false;
            continue;
        }
        current += c;
    }

    if (started || !current.empty())
        tokens.push_back(current);
    return tokens;
}

std::optional<std::string_view> StripPrefix(std::string_view message)
{
    for (std::string_view prefix : kPrefixes)
        if (message.size() > prefix.size() && message.compare(0, prefix.size(), prefix) == 0)
            return message.substr(prefix.size());
    return std::nullopt;
}

}  // namespace VoltMod::CommandSyntax
