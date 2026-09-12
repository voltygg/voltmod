#pragma once

#include <sqlpp23/core/basic/table.h>
#include <sqlpp23/core/basic/table_columns.h>
#include <sqlpp23/core/name/create_name_tag.h>
#include <sqlpp23/core/type_traits.h>

/**
 * @brief Declare one column of a sqlpp23 table spec.
 *
 * @p Member is the C++ member name, @p SqlName the unquoted column name, @p DataType a sqlpp23
 * data type (wrap it in `std::optional` when the column is nullable), and @p HasDefault
 * `std::true_type` when the column may be left out of an insert.
 *
 * @code
 * struct Bans_
 * {
 *     VOLTMOD_COLUMN(id, id, sqlpp::integral, std::true_type);
 *     VOLTMOD_COLUMN(steamId, steam_id, sqlpp::text, std::false_type);
 *     VOLTMOD_COLUMN(reason, reason, std::optional<sqlpp::text>, std::true_type);
 *
 *     SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(bans, bans);
 *     template <typename T>
 *     using _table_columns = sqlpp::table_columns<T, id, steamId, reason>;
 *     using _required_insert_columns = sqlpp::detail::type_set<sqlpp::column_t<sqlpp::table_t<Bans_>, steamId>>;
 * };
 * using Bans = sqlpp::table_t<Bans_>;
 * @endcode
 */
#define VOLTMOD_COLUMN(Member, SqlName, DataType, HasDefault)   \
    struct Member                                               \
    {                                                           \
        SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(SqlName, Member); \
        using data_type = DataType;                             \
        using has_default = HasDefault;                         \
    }
