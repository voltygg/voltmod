#pragma once

#include <sqlpp23/sqlpp23.h>
#include <type_traits>

namespace VoltMod
{

/** @p HasDefault argument of @ref VOLTMOD_COLUMN: the insert may leave this column out. */
using HasDefault = std::true_type;

/** @p HasDefault argument of @ref VOLTMOD_COLUMN: every insert must supply this column. */
using Required = std::false_type;

}  // namespace VoltMod

/**
 * @brief Declare one column of a sqlpp23 table spec.
 *
 * @p Member is the C++ member name, @p SqlName the unquoted column name, @p DataType a sqlpp23
 * data type (wrap it in `std::optional` when the column is nullable), and @p HasDefault
 * @ref VoltMod::HasDefault or @ref VoltMod::Required.
 *
 * Most tables are generated from the schema by `sqlpp23-ddl2cpp`; reach for this only when
 * hand-writing a spec.
 *
 * @code
 * struct Bans_
 * {
 *     VOLTMOD_COLUMN(id, id, sqlpp::integral, VoltMod::HasDefault);
 *     VOLTMOD_COLUMN(steamId, steam_id, sqlpp::integral, VoltMod::Required);
 *     VOLTMOD_COLUMN(reason, reason, std::optional<sqlpp::text>, VoltMod::HasDefault);
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
