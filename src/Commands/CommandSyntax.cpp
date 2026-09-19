#include "Commands/CommandSyntax.hpp"

namespace VoltMod::CommandSyntax
{

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
    if (message.size() > ChatPrefix.size() && message.starts_with(ChatPrefix))
        return message.substr(ChatPrefix.size());
    return std::nullopt;
}

}  // namespace VoltMod::CommandSyntax
