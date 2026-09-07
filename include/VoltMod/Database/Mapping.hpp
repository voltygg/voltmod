#pragma once

#include <VoltMod/Database/Column.hpp>
#include <cstddef>
#include <format>
#include <optional>
#include <pqxx/pqxx>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace VoltMod
{

/**
 * @brief Row parsing and SQL generation from an entity's column table (see Column.hpp).
 *
 * The entity declares each column once; these helpers derive row parsing (@ref FromRow,
 * @ref FromResult), the INSERT statement and its parameters (@ref InsertSql, @ref InsertParams),
 * and explicit-column SELECTs (@ref SelectSql). A `std::optional` member maps to a nullable
 * column. Bespoke UPDATE and WHERE clauses stay hand-written - those are the part worth reading at
 * the call site.
 */

/** Visit every column of @p T in declaration order. */
template <class T>
constexpr void ForEachColumn(auto&& fn)
{
    std::apply([&](const auto&... columns) { (fn(columns), ...); }, T::Columns());
}

/**
 * Visit every column an INSERT writes: all of them but the key, which the database generates.
 *
 * @ref InsertSql and @ref InsertParams both walk this, so the column list and the value list
 * cannot disagree about which columns are in play or what order they come in.
 */
template <class T>
constexpr void ForEachInsertColumn(auto&& fn)
{
    ForEachColumn<T>([&](const auto& column) {
        if (std::string_view(column.Name) != T::Key)
            fn(column);
    });
}

/** How many columns an INSERT writes. */
template <class T>
constexpr std::size_t InsertColumnCount()
{
    std::size_t count = 0;
    ForEachInsertColumn<T>([&](const auto&) { ++count; });
    return count;
}

/** Whether a mapped member is nullable. */
template <class M>
inline constexpr bool IsOptionalColumn = false;
template <class M>
inline constexpr bool IsOptionalColumn<std::optional<M>> = true;

/** Read one column of @p row into its member, leaving a nullable one empty when the field is null. */
template <class T, class M>
void AssignColumn(T& out, const Column<T, M>& column, const pqxx::row& row)
{
    const pqxx::field field = row[column.Name];
    if constexpr (IsOptionalColumn<M>)
    {
        if (field.is_null())
            out.*(column.Member) = std::nullopt;
        else
            out.*(column.Member) = field.template as<typename M::value_type>();
    }
    else
    {
        out.*(column.Member) = field.template as<M>();
    }
}

/** Comma-separated column names, for a SELECT list or an INSERT column list. */
template <class T>
std::string ColumnNames(bool excludeKey)
{
    std::string out;
    auto append = [&](const auto& column) {
        if (!out.empty())
            out += ", ";
        out += column.Name;
    };

    if (excludeKey)
        ForEachInsertColumn<T>(append);
    else
        ForEachColumn<T>(append);
    return out;
}

/** Map one row into a default-constructed T, column by column. */
template <class T>
T FromRow(const pqxx::row& row)
{
    T out{};
    ForEachColumn<T>([&](const auto& column) { AssignColumn(out, column, row); });
    return out;
}

/** Map every row of a result. */
template <class T>
std::vector<T> FromResult(const pqxx::result& result)
{
    std::vector<T> out;
    out.reserve(result.size());
    for (const auto& row : result)
        out.push_back(FromRow<T>(row));
    return out;
}

/** "INSERT INTO {table} (c1..cn) VALUES ($1..$n) RETURNING {key}" - the key column excluded. */
template <class T>
const std::string& InsertSql()
{
    static const std::string sql = [] {
        std::string placeholders;
        for (std::size_t i = 1; i <= InsertColumnCount<T>(); ++i)
        {
            if (i > 1)
                placeholders += ", ";
            placeholders += std::format("${}", i);
        }
        return std::format("INSERT INTO {} ({}) VALUES ({}) RETURNING {}", T::Table, ColumnNames<T>(true), placeholders,
                           T::Key);
    }();
    return sql;
}

/** The values for @ref InsertSql, in the same order its placeholders are numbered. */
template <class T>
pqxx::params InsertParams(const T& entity)
{
    pqxx::params params;
    ForEachInsertColumn<T>([&](const auto& column) { params.append(entity.*(column.Member)); });
    return params;
}

/** "SELECT c1..cn FROM {table}[ WHERE {where}]" - explicit columns, stable against schema drift. */
template <class T>
std::string SelectSql(std::string_view where = {})
{
    std::string sql = std::format("SELECT {} FROM {}", ColumnNames<T>(false), T::Table);
    if (!where.empty())
    {
        sql += " WHERE ";
        sql += where;
    }
    return sql;
}

}  // namespace VoltMod
