#include <cassert>

#include <sqlpp23/mysql/mysql.h>
#include <sqlpp23/postgresql/postgresql.h>
#include <sqlpp23/sqlite3/sqlite3.h>
#include <sqlpp23/sqlpp23.h>

struct Point_ {
  struct Id {
    SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(id, id);
    using data_type = sqlpp::integral;
    using has_default = std::true_type;
  };
  SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(point, point);
  template <typename T>
  using _table_columns = sqlpp::table_columns<T, Id>;
  using _required_insert_columns = sqlpp::detail::type_set<>;
};
using Point = sqlpp::table_t<Point_>;

int main() {
  sqlpp::sqlite3::connection db{
      sqlpp::sqlite3::connection_config{":memory:", SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE}};
  db("CREATE TABLE point (id INTEGER PRIMARY KEY)");

  const auto point = Point{};
  for (const auto& row : db(sqlpp::select(point.id).from(point))) {
    assert(row.id != 0);
  }
  return 0;
}
