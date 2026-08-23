#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace CS2Kit::Core
{

/** @brief Static utilities for converting between SteamID formats (64-bit, SteamID2, SteamID3). */
class SteamId
{
public:
    static std::string ToSteamId3(int64_t steamId64);
    static std::string ToSteamId(int64_t steamId64);
    static std::optional<int64_t> FromSteamId3(const std::string& steamId3);
    static std::optional<int64_t> FromSteamId(const std::string& steamId);
    static bool IsValid(int64_t steamId64);
    static uint32_t GetAccountId(int64_t steamId64);

private:
    static constexpr int64_t SteamId64Base = 76561197960265728LL;
};

}  // namespace CS2Kit::Core
