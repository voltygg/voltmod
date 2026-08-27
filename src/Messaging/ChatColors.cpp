#include <VoltMod/Core/Strings.hpp>
#include <VoltMod/Messaging/ChatColors.hpp>
#include <unordered_map>

namespace VoltMod::ChatColors
{

// Map of color names to their corresponding control codes. Keys are lowercase for case-insensitive lookup.
static const std::unordered_map<std::string, std::string_view> kNameTable = {
    {"default", Default},   {"white", White},       {"darkred", DarkRed},     {"lightpurple", LightPurple},
    {"green", Green},       {"olive", Olive},       {"lime", Lime},           {"red", Red},
    {"gray", Gray},         {"grey", Grey},         {"yellow", Yellow},       {"lightyellow", LightYellow},
    {"silver", Silver},     {"bluegrey", BlueGrey}, {"lightblue", LightBlue}, {"blue", Blue},
    {"darkblue", DarkBlue}, {"purple", Purple},     {"magenta", Magenta},     {"lightred", LightRed},
    {"gold", Gold},         {"orange", Orange},
};

std::string_view ParseNamed(std::string_view name)
{
    if (name.empty())
        return Default;

    auto it = kNameTable.find(Strings::ToLower(std::string(name)));
    return it != kNameTable.end() ? it->second : Default;
}

std::string Strip(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text)
    {
        if (static_cast<unsigned char>(c) > 0x10)  // 0x01-0x10 are chat color escape bytes
            out.push_back(c);
    }
    return out;
}

}  // namespace VoltMod::ChatColors
