"""How each driver spells the migration placeholders.

A mirror of `DialectFor` in `include/VoltMod/Database/Migrator.hpp`, for the DDL parser behind
the table specs. `tests/test_database.py` fails when the two drift.
"""

import re

#: Driver -> placeholder -> replacement. `ON_CONFLICT` is handled in `resolve`: only Postgres
#: renders a clause for it.
DIALECTS: dict[str, dict[str, str]] = {
    "postgres": {
        "ID": "BIGSERIAL PRIMARY KEY",
        "NOW": "EXTRACT(EPOCH FROM NOW())::BIGINT",
        "TRUE": "TRUE",
        "FALSE": "FALSE",
        "INSERT_IF_ABSENT": "INSERT INTO",
    },
    "mariadb": {
        "ID": "BIGINT AUTO_INCREMENT PRIMARY KEY",
        "NOW": "(UNIX_TIMESTAMP())",
        "TRUE": "TRUE",
        "FALSE": "FALSE",
        "INSERT_IF_ABSENT": "INSERT IGNORE INTO",
    },
    "sqlite": {
        # 1/0 rather than TRUE/FALSE: it is what an existing database's stored schema text says.
        "ID": "INTEGER PRIMARY KEY AUTOINCREMENT",
        "NOW": "(strftime('%s','now'))",
        "TRUE": "1",
        "FALSE": "0",
        "INSERT_IF_ABSENT": "INSERT OR IGNORE INTO",
    },
}

DRIVERS = tuple(DIALECTS)

_TOKEN =re.compile(r"@([A-Z_]+(?:\([^)@]*\))?)@")


def resolve(sql: str, driver: str) -> str:
    """Substitute every placeholder in `sql` for `driver`; an unknown one is an error."""
    values = DIALECTS[driver]

    def replace(match: re.Match[str]) -> str:
        name = match.group(1)
        if name.startswith("ON_CONFLICT("):
            columns = name[len("ON_CONFLICT(") : -1]
            return f"ON CONFLICT ({columns}) DO NOTHING" if driver == "postgres" else ""
        if name not in values:
            raise SystemExit(f"ERROR: unknown migration placeholder @{name}@")
        return values[name]

    return _TOKEN.sub(replace, sql)
