# Database {#database_guide}

[TOC]

`VoltMod/Database/` is an optional asynchronous PostgreSQL layer. One worker
owns the connection, while completions return to the game thread. Column tables
can generate row parsing and common `INSERT` or `SELECT` SQL.

Compiled only when `VOLTMOD_ENABLE_POSTGRES` is `ON` (default `OFF`); plugins without a database never pull libpqxx.

```python
# conanfile.py
default_options = {"voltmod/*:with_postgres": True}
```

## The threading model

- `Query` and `Exec` enqueue work and return immediately. Query callbacks run on
  the game thread and may use players, menus, and plugin managers.
- Jobs run FIFO, so a queued write is visible to a later read.
- Blocking variants use the same queue but wait. Restrict them to load-time work,
  migrations, and explicit operator reloads.
- Connections open lazily and recover after a drop. `connectTimeoutSec` defaults
  to 5 seconds and bounds each attempt.

## Start / Stop

```cpp
// Db is a PostgresDatabase member of your App, declared above everything that uses it.
// It takes the scheduler that drives its per-frame completion delivery:
//     VoltMod::PostgresDatabase Db{Runtime.Scheduler};
if (!Db.Start(Config.Get().database))
{
    Log::Warn("Database unavailable, running degraded.");
    return true;                           // your call: degrade or reject the load
}
```

`Start` spawns the worker and verifies connectivity with a ping. It returns `false` when the database is unreachable, so you can degrade instead of queueing into the void.

Call `Stop` from your `App` destructor rather than next to `Start`. It lets
queued writes finish (a ban issued just before unload must land) and drops undispatched
completions, so it must run after the managers those callbacks would touch have
been destroyed.

`Stop(stopDeadline = 5s)` rejects new work and lets queued jobs finish until the
deadline. It then releases blocked waiters with failures and discards
undelivered callbacks because their target state is being destroyed.

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
         [](VoltMod::DbResult<pqxx::result> result) {
             if (!result)
                 return;                         // already logged
             int count = (*result)[0][0].as<int>();
             /* touch managers, players, menus: this is the game thread */
         });
```

The name doubles as the prepared-statement key (prepared once per connection) and the log label. Never put secrets in it.

Load-time variants: `QueryBlocking(name, sql, params)` returns the `DbResult` directly; `WithConnection(fn)` hands `fn` the live connection on the worker for multi-statement work that manages its own transactions (this is what the migration runner uses).

## Row mapping

Declare each entity's table shape once and the framework generates the repetitive SQL and parsing (`Database/Column.hpp` + `Mapping.hpp`):

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
            VoltMod::Column{"id", &Ban::Id},
            VoltMod::Column{"steam_id", &Ban::SteamId},
            VoltMod::Column{"reason", &Ban::Reason},
            VoltMod::Column{"removed_at", &Ban::RemovedAt},
        };
    }
};
```

The mapping helpers then provide:

```cpp
using VoltMod::DbResult;
using VoltMod::FromResult;
using VoltMod::InsertParams;
using VoltMod::InsertSql;
using VoltMod::SelectSql;

// SELECT with explicit columns (stable against schema drift), rows -> entities:
auto rows = db.QueryBlocking("bans_active", SelectSql<Ban>("removed_at IS NULL"));
std::vector<Ban> bans = rows ? FromResult<Ban>(*rows) : std::vector<Ban>{};

// INSERT: column list, $n placeholders, and params all derived; key column excluded,
// "RETURNING id" appended so you can backfill the id:
db.Query("ban_insert", InsertSql<Ban>(), InsertParams(ban),
         [&punishments, steamId](DbResult<pqxx::result> r) {
             if (r && !r->empty())
                 punishments.BackfillBanId(steamId, (*r)[0][0].as<int64_t>());
         });
```

Bespoke UPDATE/WHERE SQL stays hand-written; that is the part worth seeing at
the call site.

## Cache-first reads

Gameplay reads should use an in-memory cache. Update the cache immediately and
queue persistence work:

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

`RunMigrations(db, dir, options)` applies newer `NNNN_*.sql` files in order,
using one transaction per file and a session advisory lock. A missing directory
is a logged no-op. Call it from `OnLoad` after `Start`:

```cpp
if (!VoltMod::RunMigrations(db, "addons/my-plugin/configs/migrations",
                           {.TableName = "schema_migrations", .AdvisoryLockKey = 727274}))
    return false;   // don't run against an out-of-date schema
```

Plugins sharing a database need distinct table names *and* distinct lock keys. The table name is interpolated into SQL, so it is validated against `[A-Za-z_][A-Za-z0-9_]*`.

## Config

`PostgresConfig` fields are lowercase so a JSON section maps onto them directly, and reflection needs no mapper - embedding it in your settings struct is all there is to do (see @ref config_guide). `sslMode` defaults to `prefer`, and `connectTimeoutSec` (default 5) bounds every reconnect attempt.
