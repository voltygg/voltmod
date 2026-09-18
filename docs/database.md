# Database {#database_guide}

[TOC]

@ref VoltMod::Database is async access to Postgres, MariaDB or SQLite, chosen at runtime from
config. One worker thread owns the connection, jobs run FIFO on it, and completions replay on the
game thread each frame, so a callback may touch players, menus and plugin managers.

Ask for it in CMake; the package always ships it, but a plugin links it and the sqlpp23 connectors
only on request:

```cmake
voltmod_add_plugin(my-plugin FEATURES DATABASE)
```

```cpp
// Db is a VoltMod::Database member of your App, declared above everything that uses it, and
// constructed with the scheduler that drives completion delivery: VoltMod::Database Db{Runtime.Scheduler};
if (!Db.Connect(Config.Get().database))
{
    Log::Warn("Database unavailable, running degraded.");
    return true;                           // your call: degrade or reject the load
}
```

`Connect` parses the driver, spawns the worker and verifies connectivity with a ping. It returns
false on an invalid config or an unreachable database, so the plugin can degrade instead of
queueing work that cannot run.

Call `Stop` from the `App` destructor, after the managers owning its callbacks are gone. It lets
queued jobs finish within `stopDeadline` (5s by default) so a ban written just before unload
still lands, then drops the rest, releases blocked waiters with a failure, and destroys
undispatched completions unrun. It is idempotent and the destructor calls it.

## Config

@ref VoltMod::DatabaseConfig fields are lowercase, so a JSON section maps onto them with no mapper:

| Field | Default | Notes |
| --- | --- | --- |
| `driver` | `"postgres"` | `"postgres"`, `"mariadb"` or `"sqlite"`; anything else fails `Connect` |
| `host` | `"localhost"` | ignored by sqlite |
| `port` | `0` | `0` uses the driver default (5432 Postgres, 3306 MariaDB); ignored by sqlite |
| `database` | `"voltmod_server"` | |
| `username` | `"voltmod_plugin"` | |
| `password` | `""` | |
| `sslMode` | `"prefer"` | Postgres: `disable`, `allow`, `prefer`, `require`, `verify-ca`, `verify-full`; MariaDB turns TLS on for `require` and the verify modes; ignored by sqlite |
| `connectTimeoutSec` | `5` | bounds every (re)connect attempt |
| `path` | `""` | sqlite file, relative to the game dir; `":memory:"` is allowed |

```cpp
struct Settings
{
    VoltMod::DatabaseConfig database;   // "database": { "driver": ..., "host": ... }
};
```

```jsonc
"database": { "driver": "postgres", "host": "localhost", "port": 0, "database": "voltmod_server",
              "username": "voltmod_plugin", "password": "...", "sslMode": "prefer" }

"database": { "driver": "sqlite", "path": "addons/voltmod/plugins/my-plugin/data.db" }
```

## Running work

A job is a callable taking `auto& conn`, compiled once per connection type. It must return the same
type on every driver, so dialect differences go in `if constexpr` branches on `IsPostgres`,
`IsMariaDb` and `IsSqlite`. A bare name blocks, an `Async` name returns first.

| Call | Behavior |
| --- | --- |
| `RunAsync(name, fn, onDone)` | the gameplay path: enqueue and return; `onDone` runs on the game thread when the worker finishes |
| `Run(name, fn)` | enqueue and wait. Load time only - `Plugin::Load`, migrations, an admin reload - never per frame or per event |
| `RunOr(name, fn, fallback)` | `Run`, folding a failure into a value |

`name` is a log label. An `onDone` taking the value alone fires only on success, the failure having
already been logged; taking the whole `Result<T>` shows it. `Run` returns `VoltMod::Result<T>`: a
job that never ran because the database is stopping or down carries `ErrorCode::NotReady`, and one
that threw carries `ErrorCode::Failed` with the driver's message in `Error::Detail`.

Capture by value - the job outlives the call that enqueued it.

```cpp
Bans t;

// Typed select; the rows arrive on the game thread.
db.RunAsync("bans_active",
            [t](auto& conn) {
                std::vector<int64_t> ids;
                for (const auto& row : conn(select(all_of(t)).from(t).where(t.reason.is_null())))
                    ids.push_back(row.id);
                return ids;
            },
            [](std::vector<int64_t> ids) { /* announce the active bans */ });

// Insert returning the id: Postgres reads the SERIAL sequence, the others report last-insert-id
db.RunAsync("ban_insert",
            [t, steamId, reason](auto& conn) {
                return VoltMod::Insert(conn, insert_into(t).set(t.steamId = steamId, t.reason = reason), "bans");
            },
            [](int64_t id) { /* record the ban id */ });

db.RunAsync("ban_lift", [t, banId](auto& conn) { conn(update(t).set(t.reason = std::nullopt).where(t.id == banId)); });
```

MariaDB has neither `RETURNING` nor `ON CONFLICT`, so a portable upsert is update-then-insert:
`VoltMod::Upsert(conn, update, insert)` runs the update and inserts only when nothing matched. It
is not atomic, so back the natural key with a UNIQUE constraint and a race fails one job rather
than duplicating a row.

`conn("...")` runs one raw statement and is for DDL and DML only - never a raw `SELECT` on MariaDB,
which leaves an unread result set on the connection.

## Declaring tables

Generate the table header from the migrations:

```bash
voltmod database tables --migrations <plugin>/configs/migrations \
    --header <plugin>/src/Database/Tables/Schema.hpp \
    --namespace MyPlugin::Database::Tables
```

It renders the migrations for Postgres, runs `sqlpp23-ddl2cpp`, and writes the header, skipping
the indexes and inserts its grammar cannot parse - the migrations stay the one source of truth.
Commit the header and run the same command with `--check` in your lint task, so a schema change
that skips the generator fails the build.

Hand-write a spec only where there is no DDL to read, using `VOLTMOD_COLUMN` from
`<VoltMod/Database/Table.hpp>`:

```cpp
struct Bans_
{
    VOLTMOD_COLUMN(id, id, sqlpp::integral, VoltMod::HasDefault);
    VOLTMOD_COLUMN(steamId, steam_id, sqlpp::integral, VoltMod::Required);
    VOLTMOD_COLUMN(reason, reason, std::optional<sqlpp::text>, VoltMod::HasDefault);

    SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(bans, bans);
    template <typename T>
    using _table_columns = sqlpp::table_columns<T, id, steamId, reason>;
    using _required_insert_columns = sqlpp::detail::type_set<sqlpp::column_t<sqlpp::table_t<Bans_>, steamId>>;
};
using Bans = sqlpp::table_t<Bans_>;
```

Wrap the data type in `std::optional` for a nullable column. The last argument is
`VoltMod::HasDefault` when an insert may leave the column out and `VoltMod::Required` when it must
not.

## Migrations

Migration files live at `<dir>/NNNN_name.sql`, one statement per `;`. Procedure bodies are not
supported: MariaDB and SQLite run raw SQL one statement at a time.

Write each file once, in dialect-free SQL. The runner substitutes a placeholder wherever the three
drivers disagree, a closed set of five:

| Placeholder | Postgres | MariaDB | SQLite |
| --- | --- | --- | --- |
| `@ID@` | `BIGSERIAL PRIMARY KEY` | `BIGINT AUTO_INCREMENT PRIMARY KEY` | `INTEGER PRIMARY KEY AUTOINCREMENT` |
| `@NOW@` | `EXTRACT(EPOCH FROM NOW())::BIGINT` | `(UNIX_TIMESTAMP())` | `(strftime('%s','now'))` |
| `@TRUE@` / `@FALSE@` | `TRUE` / `FALSE` | `TRUE` / `FALSE` | `1` / `0` |
| `@INSERT_IF_ABSENT@` | `INSERT INTO` | `INSERT IGNORE INTO` | `INSERT OR IGNORE INTO` |
| `@ON_CONFLICT(cols)@` | `ON CONFLICT (cols) DO NOTHING` | *(nothing)* | *(nothing)* |

The last two go together: Postgres puts its clause at the end of the statement and the other two
carry the same meaning in their `INSERT` verb.

```sql
CREATE TABLE IF NOT EXISTS bans (
  id @ID@,
  steam_id BIGINT UNIQUE NOT NULL,
  is_active BOOLEAN NOT NULL DEFAULT @TRUE@,
  created_at BIGINT NOT NULL DEFAULT @NOW@
);

@INSERT_IF_ABSENT@ bans (steam_id) VALUES (76561198000000000)
@ON_CONFLICT(steam_id)@;
```

An unknown `@TOKEN@` fails the migration rather than applying a statement with a hole in it.
Anything the set does not cover belongs in a driver-specific migration.
`voltmod database sql <file-or-dir> --driver <name>` prints what a driver will actually run, which
is also how an operator applies a hand-run seed file.

```cpp
if (!VoltMod::RunMigrations(db, "addons/voltmod/plugins/my-plugin/configs/migrations",
                            {.HistoryTable = "schema_migrations", .LockKey = 727274}))
    return false;   // don't run against an out-of-date schema
```

Each file runs in its own transaction; MariaDB DDL auto-commits regardless, so a failed file there
leaves the tables it already created with the version row as the source of truth. The history
table records applied versions and is validated against `[A-Za-z_][A-Za-z0-9_]*` before being
interpolated into SQL. A session lock - a Postgres advisory lock, a MariaDB named lock, SQLite's
own write lock - serializes two plugin loads racing on the same database. Plugins sharing a
database need distinct history tables *and* distinct lock keys.

`RunMigrations` returns @ref VoltMod::MigrationResult (`Success`, `Applied`, `CurrentVersion`),
contextually convertible to bool. A missing directory is a logged no-op, not a failure.

## Licensing

Linking all three connectors statically carries their upstream licenses: libpq is the PostgreSQL
licence (permissive), sqlite3 is public domain, and mariadb-connector-c is LGPL - static linking
obliges you to let users relink against a different connector version. See `conanfile.py`.
