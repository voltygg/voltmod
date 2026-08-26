#include <VoltMod/Core/SteamId.hpp>
#include <charconv>
#include <format>
#include <string_view>

namespace VoltMod
{

std::string SteamId::ToSteamId3(int64_t steamId64)
{
    uint32_t accountId = GetAccountId(steamId64);
    return std::format("[U:1:{}]", accountId);
}

std::string SteamId::ToSteamId(int64_t steamId64)
{
    uint32_t accountId = GetAccountId(steamId64);
    uint32_t authServer = accountId % 2;
    uint32_t accountNum = accountId / 2;
    return std::format("STEAM_0:{}:{}", authServer, accountNum);
}

std::optional<int64_t> SteamId::FromSteamId3(const std::string& steamId3)
{
    // Was regex `\[U:1:(\d+)\]` (full match).
    constexpr std::string_view prefix = "[U:1:";
    if (!steamId3.starts_with(prefix) || steamId3.size() <= prefix.size() + 1 || steamId3.back() != ']')
    {
        return std::nullopt;
    }

    const char* first = steamId3.data() + prefix.size();
    const char* last = steamId3.data() + steamId3.size() - 1;  // the trailing ']'
    uint32_t accountId = 0;
    auto [ptr, ec] = std::from_chars(first, last, accountId);

    if (ec != std::errc{} || ptr != last)
    {
        return std::nullopt;
    }

    return SteamId64Base + accountId;
}

std::optional<int64_t> SteamId::FromSteamId(const std::string& steamId)
{
    // Was regex `STEAM_[0-5]:([01]):(\d+)` (full match). Fixed shape after the prefix:
    // <universe 0-5> ':' <auth 0|1> ':' <accountNum>.
    constexpr std::string_view prefix = "STEAM_";
    if (!steamId.starts_with(prefix) || steamId.size() < prefix.size() + 4)
        return std::nullopt;

    const char universe = steamId[prefix.size()];
    const char auth = steamId[prefix.size() + 2];
    if (universe < '0' || universe > '5' || auth < '0' || auth > '1' || steamId[prefix.size() + 1] != ':' ||
        steamId[prefix.size() + 3] != ':')
        return std::nullopt;

    const char* first = steamId.data() + prefix.size() + 4;
    const char* last = steamId.data() + steamId.size();
    uint32_t accountNum = 0;
    auto [ptr, ec] = std::from_chars(first, last, accountNum);
    if (ec != std::errc{} || ptr != last)  // (\d+) requires ≥1 digit and no trailing junk
        return std::nullopt;

    const uint32_t accountId = accountNum * 2 + static_cast<uint32_t>(auth - '0');
    return SteamId64Base + accountId;
}

bool SteamId::IsValid(int64_t steamId64)
{
    return steamId64 >= SteamId64Base && steamId64 < (SteamId64Base + 0x100000000LL);  // account IDs are 32-bit
}

uint32_t SteamId::GetAccountId(int64_t steamId64)
{
    return static_cast<uint32_t>(steamId64 - SteamId64Base);
}

}  // namespace VoltMod
