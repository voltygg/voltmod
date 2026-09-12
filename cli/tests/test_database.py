"""Cover the migration renderer, and keep it in step with the C++ resolver.

If the two disagreed, the generated table specs would describe a schema the server never creates.
"""

import re
from pathlib import Path

import pytest
from voltmod.builder.database import dialect
from voltmod.tools import framework_root

MIGRATOR_HPP = framework_root() / "include/VoltMod/Database/Migrator.hpp"

# The `DialectFor` arms, in the order the struct declares its members.
MEMBERS = ("ID", "NOW", "TRUE", "FALSE", "INSERT_IF_ABSENT")
CPP_DRIVER_NAMES = {"Postgres": "postgres", "MariaDb": "mariadb", "Sqlite": "sqlite"}


def cpp_dialects():
    """Read the replacement text out of `DialectFor` in the C++ header."""
    source = MIGRATOR_HPP.read_text(encoding="utf-8")
    body = source[source.index("inline Dialect DialectFor") :]
    body = body[: body.index("\n}")]

    arms = re.findall(r"case Driver::(\w+):\s*(?:// [^\n]*\n\s*)?return \{(.+?)\};", body, re.S)

    out = {}
    for arm, values in arms:
        literals = re.findall(r'"([^"]*)"', values)
        out[CPP_DRIVER_NAMES[arm]] = dict(zip(MEMBERS, literals, strict=True))
    return out


def test_python_and_cpp_dialect_tables_agree():
    assert cpp_dialects() == dialect.DIALECTS


@pytest.mark.parametrize("driver", dialect.DRIVERS)
def test_every_placeholder_resolves(driver):
    sql = (
        "CREATE TABLE t (id @ID@, at BIGINT DEFAULT @NOW@, "
        "ok BOOLEAN DEFAULT @TRUE@, no BOOLEAN DEFAULT @FALSE@);\n"
        "@INSERT_IF_ABSENT@ t (id) VALUES (1) @ON_CONFLICT(id)@;"
    )
    assert "@" not in dialect.resolve(sql, driver)


def test_conflict_clause_is_postgres_only():
    sql = "@INSERT_IF_ABSENT@ t (a, b) VALUES (1, 2) @ON_CONFLICT(a, b)@"
    assert dialect.resolve(sql, "postgres").endswith("ON CONFLICT (a, b) DO NOTHING")
    assert dialect.resolve(sql, "mariadb").startswith("INSERT IGNORE INTO")
    assert "CONFLICT" not in dialect.resolve(sql, "sqlite")


def test_an_unknown_placeholder_is_refused():
    with pytest.raises(SystemExit):
        dialect.resolve("id @MADE_UP@", "postgres")


def test_migrations_render_in_version_order(tmp_path: Path):
    from voltmod.builder.database import command

    (tmp_path / "0002_second.sql").write_text("CREATE TABLE b (id @ID@);\n", encoding="utf-8")
    (tmp_path / "0001_first.sql").write_text("CREATE TABLE a (id @ID@);\n", encoding="utf-8")
    (tmp_path / "notes.sql.bak").write_text("ignored", encoding="utf-8")

    rendered = command.render(tmp_path, "sqlite")
    assert rendered.index("TABLE a") < rendered.index("TABLE b")
    assert "ignored" not in rendered
