# Database {#database_guide}

[TOC]

`CS2Kit::Database` is an opt-in, **async-first** PostgreSQL layer. One worker thread owns the only connection; the game thread never blocks on the database during play. Row parsing and INSERT/SELECT SQL are generated from a per-entity column table, so a repository method is a query and a callback - nothing else.

Compiled only when `CS2KIT_ENABLE_POSTGRES` is `ON` (default `OFF`); plugins without a database never pull libpqxx.

```python
# conanfile.py
default_options = {"cs2-kit/*:with_postgres": True}
```

## The threading model

- `Query` / `Exec` are the gameplay path: enqueue, return immediately. `Query` completions are queued and replayed **on the game thread** (a per-frame pump self-registers in `Start`), so callbacks may touch players, menus, and your managers freely.
- Jobs run FIFO on the worker, so a write enqueued before a read is visible to that read - counting rows you just inserted works without ceremony.
- The `*Blocking` variants ride the same queue but wait. They exist for load time only: `OnLoad`, migrations, an explicit admin reload. Never call them per-frame or per-event.
- The connection opens lazily and reopens after a drop; `PostgresConfig::connectTimeoutSec` (default 5) bounds every attempt so a dead database can't hang a query or unload.

## Start / Stop

```cpp
auto& db = App().Db;                       // a PostgresDatabase member in your Managers

if (!db.Start(App().Config.Get().database))
{
    Log::Warn("Database unavailable - running degraded.");
    return true;                           // your call: degrade or reject the load
}
Defer([] { App().Db.Stop(); });
```

`Start` spawns the worker and verifies connectivity with a ping - it returns `false` when the database is unreachable so you can degrade instead of queueing into the void.

`Stop(drainDeadline = 5s)` is deliberate about what happens to in-flight work: new work is dropped with a log line; already-queued jobs drain within the deadline (a ban written just before unload must land); anything past the deadline is dropped with a warning; blocked waiters are released with a failed result; and undispatched completions are destroyed unrun - the state they would touch is going away.

## Queries

```cpp
// Fire-and-forget write; failures are logged under the given name:
db.Exec("audit_insert",
        "INSERT INTO admin_activity (admin_id, action) VALUES ($1, $2)",
        pqxx::params{steamId, action});

// Async read; the callback runs on the game thread on a later frame:
db.Query("count_recent_bans",
         "SELECT COUNT(*) FROM bans WHERE admin_id = $1 AND created_at > $2",
         pqxx::params{steamId, windowStart},
         [](CS2Kit::DbResult<pqxx::result> result) {
             if (!result)
                 return;                         // already logged
             int count = (*result)[0][0].as<int>();
             /* touch managers, players, menus - this is the game thread */
         });
```

The name doubles as the prepared-statement key (prepared once per connection) and the log label. Never put secrets in it.

Load-time variants: `QueryBlocking(name, sql, params)` returns the `DbResult` directly; `WithConnection(fn)` hands `fn` the live connection on the worker for multi-statement work that manages its own transactions (this is what the migration runner uses).

## Row mapping

Declare each entity's table shape once and the kit generates the repetitive SQL and parsing (`Database/Column.hpp` + `Mapping.hpp`):

```cpp
struct Ban
{
    int64_t Id = 0;
    int64_t SteamId = 0;
    std::string Reason;
    std::optional<int64_t> RemovedAt;    // optional = nullable column

    static constexpr const char* Table = "bans";
    static constexpr const char* Key = "id";
    static constexpr auto Columns()
    {
        return std::tuple{
            CS2Kit::Column{"id", &Ban::Id},
            CS2Kit::Column{"steam_id", &Ban::SteamId},
            CS2Kit::Column{"reason", &Ban::Reason},
            CS2Kit::Column{"removed_at", &Ban::RemovedAt},
        };
    }
};
```

What that buys:

```cpp
using namespace CS2Kit;

// SELECT with explicit columns (stable against schema drift), rows -> entities:
auto rows = db.QueryBlocking("bans_active", SelectSql<Ban>("removed_at IS NULL"));
std::vector<Ban> bans = rows ? FromResult<Ban>(*rows) : std::vector<Ban>{};

// INSERT: column list, $n placeholders, and params all derived; key column excluded,
// "RETURNING id" appended so you can backfill the id:
db.Query("ban_insert", InsertSql<Ban>(), InsertParams(ban),
         [steamId](DbResult<pqxx::result> r) {
             if (r && !r->empty())
                 App().Punishments.BackfillBanId(steamId, (*r)[0][0].as<int64_t>());
         });
```

Bespoke UPDATE/WHERE SQL stays hand-written - that's the part worth reading at the call site.

## The cache-first pattern

Gameplay reads should never wait on the database. Keep the active rows in memory, update the cache synchronously when acting, and let the write ride the worker:

```cpp
void PunishmentManager::IssueBan(Ban ban)
{
    _bans[ban.SteamId] = ban;                          // effective immediately
    db.Query("ban_insert", InsertSql<Ban>(), InsertParams(ban),
             [this, steamId = ban.SteamId](auto r) { /* backfill id into the cache */ });
}
```

A periodic async sweep re-snapshots the caches (expiry, changes from other servers sharing the database). Because the worker is FIFO, an escalation query that counts prior writes ("3rd warning → auto-ban") sees them without extra synchronization.

## Migrations

`RunMigrations(db, dir, options)` scans `dir` for `NNNN_*.sql` files and applies everything above the recorded version, in order, each in its own transaction, under a session advisory lock (two servers loading against one database won't race). A missing directory is a logged no-op. It runs on the blocking path - call it from `OnLoad` right after `Start`:

```cpp
if (!CS2Kit::RunMigrations(db, "addons/my-plugin/configs/migrations",
                           {.TableName = "schema_migrations", .AdvisoryLockKey = 727274}))
    return false;   // don't run against an out-of-date schema
```

Plugins sharing a database need distinct table names *and* distinct lock keys. The table name is interpolated into SQL, so it is validated against `[A-Za-z_][A-Za-z0-9_]*`.

## Config

`PostgresConfig` fields are lowercase so a JSON section maps onto them directly; define the nlohmann mapper in your plugin, inside the `CS2Kit::Database` namespace (see @ref config_guide). `sslMode` defaults to `prefer`.
