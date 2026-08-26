#pragma once

namespace VoltMod
{

/**
 * @brief One column of an entity's mapping table: the SQL column name bound to a member pointer.
 *
 * An entity declares its table shape once, next to the struct, and the mapping helpers
 * (Mapping.hpp) generate row parsing, INSERT SQL, and parameter packs from it:
 *
 * @code
 * struct Ban
 * {
 *     int64_t Id = 0;
 *     int64_t TargetSteamId = 0;
 *     std::optional<int64_t> RemovedAt;   // nullable column
 *     ...
 *     static constexpr const char* Table = "bans";
 *     static constexpr const char* Key = "id";  // auto-generated key, excluded from INSERT
 *     static constexpr auto Columns()
 *     {
 *         using VoltMod::Column;
 *         return std::tuple{Column{"id", &Ban::Id}, Column{"target_steam_id", &Ban::TargetSteamId}, ...};
 *     }
 * };
 * @endcode
 *
 * This header is pqxx-free so entity headers stay light; include Mapping.hpp where rows are
 * actually parsed or SQL is generated.
 */
template <class T, class M>
struct Column
{
    const char* Name;
    M T::* Member;
};

}  // namespace VoltMod
