# Database {#database_guide}

[TOC]

`VoltMod::Database` is an optional asynchronous database layer over Postgres,
MariaDB, or SQLite, chosen at runtime from config. One worker thread owns the
connection; jobs run FIFO on it, and completions are queued and delivered back
on the game thread each frame, so callbacks may touch players, menus, and
plugin managers freely.

Compiled only when `VOLTMOD_ENABLE_DATABASE` is on (default off); plugins
without a database never pull sqlpp23 or its connectors.

```python
# conanfile.py
default_options = {"voltmod/*:with_database": True}
```

## Config

`VoltMod::DatabaseConfig` fields are lowercase so a JSON section maps onto them
directly:

| Field | Default | Notes |
| --- | --- | --- |
| `driver` | `"postgres"` | `"postgres"`, `"mariadb"`, or `"sqlite"`; anything else fails `Start` |
| `host` | `"localhost"` | ignored by sqlite |
| `port` | `0` | `0` uses the driver default (5432 Postgres, 3306 MariaDB); ignored by sqlite |
| `database` | `"voltmod_server"` | |
| `username` | `"voltmod_plugin"` | |
| `password` | `""` | |
| `sslMode` | `"prefer"` | Postgres: `disable`, `allow`, `prefer`, `require`, `verify-ca`, `verify-full`; MariaDB turns TLS on for `require` and the verify modes; ignored by sqlite |
| `connectTimeoutSec` | `5` | bounds every (re)connect attempt |
| `path` | `""` | sqlite file, relative to the game dir; `":memory:"` is allowed; required when `driver` is `sqlite` |

```jsonc
// Postgres
"database": { "driver": "postgres", "host": "localhost", "port": 0, "database": "voltmod_server",
               "username": "voltmod_plugin", "password": "...", "sslMode": "prefer" }

// MariaDB
"database": { "driver": "mariadb", "host": "localhost", "port": 0, "database": "voltmod_server",
              "username": "voltmod_plugin", "password": "..." }

// SQLite
"database": { "driver": "sqlite", "path": "addons/my-plugin/data.db" }
```

Embed it in your settings struct; reflection needs no mapper:

```cpp
struct Settings
{
    VoltMod::DatabaseConfig database;   // "database": { "driver": ..., "host": ... }
};
```

## Start / Stop

```cpp
// Db is a VoltMod::Database member of your App, declared above everything that uses it.
// It takes the scheduler that drives its per-frame completion delivery:
//     VoltMod::Database Db{Runtime.Scheduler};
if (!Db.Start(Config.Get().database))
{
    Log::Warn("Database unavailable, running degraded.");
    return true;                           // your call: degrade or reject the load
}
```

`Start` parses the driver, spawns the worker, and verifies connectivity with a
ping. It returns `false` on an invalid config or an unreachable database, so
the plugin can degrade instead of queueing work that cannot run.

Call `Stop` from your `App` destructor rather than next to `Start`, after the
managers that own its callbacks are gone. It lets queued jobs finish within
`stopDeadline` (default 5s) - a ban written just before unload gets to land -
then drops anything past the deadline, releases blocked waiters with a failed
result, and destroys undispatched completions unrun.

## Running work

A job is a callable taking `auto& conn`, compiled once per connection type; it
must return the same type on every driver, so dialect differences go in
`if constexpr` branches on `IsPostgres`, `IsMariaDb`, `IsSqlite`.

A bare name blocks, an `Async` name returns first:

- `RunAsync(name, fn, onDone)` is the gameplay path: enqueue and return
  immediately; `onDone` runs on the game thread once the worker finishes. Pass a
  callback taking the value alone and it fires only on success, the failure
  having already been logged; take the whole `Result<T>` to see it.
- `Run(name, fn)` enqueues the same way but waits. Use it only at load time -
  `OnLoad`, migrations, an explicit admin reload - never on a per-frame or
  per-event path. `RunOr(name, fn, fallback)` folds a failure into a value.

`Run` returns `VoltMod::Result<T>` over `Error`, the same type the rest of the
framework uses. A job that never ran, because the database is stopping or the
connection is down, carries `ErrorCode::NotReady`; one that threw carries
`ErrorCode::Failed` with the driver's message in `Error::Detail`.

```cpp
db.RunAsync("audit_insert",
            [steamId, action](auto& conn) { conn("INSERT INTO admin_activity (admin_id, action) VALUES (" +
                                                 std::to_string(steamId) + ", '" + conn.escape(action) + "')"); });

auto count = db.Run("count_recent_bans", [&](auto& conn) {
    int total = 0;
    for (const auto& row : conn(select(count(t.id)).from(t).where(t.adminId == steamId && t.createdAt > windowStart)))
        total = static_cast<int>(row.count);
    return total;
});
```

## Declaring tables

Generate them from the migrations:

```bash
voltmod database tables --migrations <plugin>/configs/migrations \
    --header <plugin>/src/Database/Tables/Schema.hpp \
    --namespace MyPlugin::Database::Tables
```

It renders the migrations for Postgres, runs the `sqlpp23-ddl2cpp` script the
sqlpp23 package ships, and writes the header. The generator skips the indexes
and inserts its grammar cannot parse, so the migrations stay the one source of
truth. Commit the header, and run the same command with `--check` in your lint
task so a schema change that skips the generator fails the build. admin-system
is the worked example.

Hand-write a spec only where there is no DDL to read, using `VOLTMOD_COLUMN`
from `<VoltMod/Database/Table.hpp>`:

```cpp
struct Bans_
{
    VOLTMOD_COLUMN(id, id, sqlpp::integral, VoltMod::HasDefault);
    VOLTMOD_COLUMN(steamId, steam_id, sqlpp::text, VoltMod::Required);
    VOLTMOD_COLUMN(reason, reason, std::optional<sqlpp::text>, VoltMod::HasDefault);

    SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(bans, bans);
    template <typename T>
    using _table_columns = sqlpp::table_columns<T, id, steamId, reason>;
    using _required_insert_columns = sqlpp::detail::type_set<sqlpp::column_t<sqlpp::table_t<Bans_>, steamId>>;
};
using Bans = sqlpp::table_t<Bans_>;
```

Wrap the data type in `std::optional` for a nullable column (`reason` above).
The last argument is `VoltMod::HasDefault` when an insert may leave the column
out, an id or anything with a database-side default, and `VoltMod::Required`
when it must not.

## Queries

Gameplay queries go through `RunAsync`, so the tick never waits on the database.
Capture by value: the job outlives the call that enqueued it.

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

// Update, with nothing to report back
db.RunAsync("ban_lift", [t, banId](auto& conn) { conn(update(t).set(t.reason = std::nullopt).where(t.id == banId)); });
```

MariaDB has neither `RETURNING` nor `ON CONFLICT`, so a portable upsert is
update-then-insert: `VoltMod::Upsert(conn, update, insert)` runs the update and
inserts only when nothing matched. It is not atomic, so back the natural key
with a UNIQUE constraint and a race fails one job rather than duplicating a row.
Reach for `IsPostgres`/`IsMariaDb`/`IsSqlite` only for a genuine dialect quirk
like this, not as a default style.

## Raw SQL

`conn("...")` runs one statement and is for DDL/DML only - never a raw
`SELECT` on MariaDB, which leaves an unread result set on the connection.

## Migrations

Migration files live at `<dir>/NNNN_name.sql`, one statement per `;`. Procedure
bodies are not supported: MariaDB and SQLite run raw SQL one statement at a
time.

Write each file once, in dialect-free SQL. The runner substitutes a placeholder
wherever the three drivers disagree, which is a closed set of five:

| Placeholder | Postgres | MariaDB | SQLite |
| --- | --- | --- | --- |
| `@ID@` | `BIGSERIAL PRIMARY KEY` | `BIGINT AUTO_INCREMENT PRIMARY KEY` | `INTEGER PRIMARY KEY AUTOINCREMENT` |
| `@NOW@` | `EXTRACT(EPOCH FROM NOW())::BIGINT` | `(UNIX_TIMESTAMP())` | `(strftime('%s','now'))` |
| `@TRUE@` / `@FALSE@` | `TRUE` / `FALSE` | `TRUE` / `FALSE` | `1` / `0` |
| `@INSERT_IF_ABSENT@` | `INSERT INTO` | `INSERT IGNORE INTO` | `INSERT OR IGNORE INTO` |
| `@ON_CONFLICT(cols)@` | `ON CONFLICT (cols) DO NOTHING` | *(nothing)* | *(nothing)* |

The last two go together, because Postgres puts its clause at the end of the
statement and the other two carry the same meaning in their `INSERT` verb:

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

An unknown `@TOKEN@` fails the migration rather than applying a statement with
a hole in it. Anything the set does not cover belongs in a driver-specific
migration, not in another placeholder. `voltmod database sql <file-or-dir>
--driver <name>` prints what a driver will actually run, which is also how an
operator applies a hand-run seed file.

```cpp
if (!VoltMod::RunMigrations(db, "addons/my-plugin/configs/migrations",
                           {.HistoryTable = "schema_migrations", .LockKey = 727274}))
    return false;   // don't run against an out-of-date schema
```

Each file runs in its own transaction (MariaDB DDL auto-commits regardless -
a failed file there leaves the tables it already created, with the version
row as the source of truth). A history table (`MigrationOptions::HistoryTable`,
interpolated into SQL and validated against `[A-Za-z_][A-Za-z0-9_]*`) records
applied versions; a session lock (Postgres advisory lock, MariaDB named lock,
SQLite's own write lock) serializes two plugin loads racing on the same
database. Plugins sharing a database need distinct history tables *and*
distinct lock keys.

`RunMigrations` returns `MigrationResult` (`Success`, `Applied`,
`CurrentVersion`), contextually convertible to bool. A missing directory is a
logged no-op, not a failure.

## Shutdown semantics

`Stop(stopDeadline = 5s)` rejects new work and lets queued jobs finish until
the deadline. It then releases blocked waiters with failures and discards
undelivered callbacks because their target state is being destroyed.

## Licensing

Linking all three connectors statically carries their upstream licenses: libpq
is PostgreSQL licence (permissive), sqlite3 is public domain, and
mariadb-connector-c is LGPL - statically linking it obliges you to let users
relink against a different connector version (see `conanfile.py`).
