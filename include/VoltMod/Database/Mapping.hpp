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

namespace VoltMod::Database
{

/**
 * @brief Row/SQL generation over an entity's Column table (see Column.hpp for the contract).
 *
 * The entity declares each column once; these helpers derive field-by-field row
 * parsing (@ref FromRow), result mapping
 * (@ref FromResult), the INSERT statement with its placeholder list (@ref InsertSql +
 * @ref InsertParams, key column excluded), and explicit-column SELECTs (@ref SelectSql).
 * `std::optional` members map to nullable columns. Bespoke UPDATE/WHERE SQL stays hand-written -
 * that is the part worth reading at the call site.
 */

namespace Detail
{

template <class M>
inline constexpr bool IsOptional = false;
template <class M>
inline constexpr bool IsOptional<std::optional<M>> = true;

template <class T, class M>
void Assign(T& out, const Column<T, M>& col, const pqxx::row& row)
{
    const pqxx::field field = row[col.Name];
    if constexpr (IsOptional<M>)
    {
        if (field.is_null())
            out.*(col.Member) = std::nullopt;
        else
            out.*(col.Member) = field.template as<typename M::value_type>();
    }
    else
    {
        out.*(col.Member) = field.template as<M>();
    }
}

template <class T>
std::string JoinColumnNames(bool excludeKey)
{
    std::string out;
    std::apply(
        [&](const auto&... cols) {
            auto add = [&](const auto& col) {
                if (excludeKey && std::string_view(col.Name) == T::Key)
                    return;
                if (!out.empty())
                    out += ", ";
                out += col.Name;
            };
            (add(cols), ...);
        },
        T::Columns());
    return out;
}

template <class T>
std::size_t CountInsertColumns()
{
    std::size_t count = 0;
    std::apply(
        [&](const auto&... cols) {
            auto add = [&](const auto& col) {
                if (std::string_view(col.Name) != T::Key)
                    ++count;
            };
            (add(cols), ...);
        },
        T::Columns());
    return count;
}

}  // namespace Detail

/** Map one row into a default-constructed T, column by column (optionals are null-aware). */
template <class T>
T FromRow(const pqxx::row& row)
{
    T out{};
    std::apply([&](const auto&... cols) { (Detail::Assign(out, cols, row), ...); }, T::Columns());
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

/** "INSERT INTO {table} (c1..cn) VALUES ($1..$n) RETURNING {key}" - key column excluded. */
template <class T>
const std::string& InsertSql()
{
    static const std::string sql = [] {
        std::string placeholders;
        const std::size_t count = Detail::CountInsertColumns<T>();
        for (std::size_t i = 1; i <= count; ++i)
        {
            if (i > 1)
                placeholders += ", ";
            placeholders += std::format("${}", i);
        }
        return std::format("INSERT INTO {} ({}) VALUES ({}) RETURNING {}", T::Table, Detail::JoinColumnNames<T>(true),
                           placeholders, T::Key);
    }();
    return sql;
}

/** The values for @ref InsertSql, in column-table order with the key excluded. */
template <class T>
pqxx::params InsertParams(const T& entity)
{
    pqxx::params params;
    std::apply(
        [&](const auto&... cols) {
            auto add = [&](const auto& col) {
                if (std::string_view(col.Name) == T::Key)
                    return;
                params.append(entity.*(col.Member));
            };
            (add(cols), ...);
        },
        T::Columns());
    return params;
}

/** "SELECT c1..cn FROM {table}[ WHERE {where}]" - explicit columns, stable against schema drift. */
template <class T>
std::string SelectSql(std::string_view where = {})
{
    std::string sql = std::format("SELECT {} FROM {}", Detail::JoinColumnNames<T>(false), T::Table);
    if (!where.empty())
    {
        sql += " WHERE ";
        sql += where;
    }
    return sql;
}

}  // namespace VoltMod::Database
