import re
from pathlib import Path

import pytest

from voltmod.database.migrations import (
    DIALECTS,
    DRIVERS,
    apply_altered_columns,
    render_migrations,
    resolve_placeholders,
)
from voltmod.errors import VoltmodError

MIGRATOR_HPP = Path(__file__).resolve().parents[2] / "include/VoltMod/Database/Migrator.hpp"

# The `DialectFor` arms, in the order the struct declares its members.
MEMBERS = ("ID", "NOW", "TRUE", "FALSE", "INSERT_IF_ABSENT")
CPP_DRIVER_NAMES = {"Postgres": "postgres", "MariaDb": "mariadb", "Sqlite": "sqlite"}


def cpp_dialects() -> dict[str, tuple[dict[str, str], bool]]:
    """`DialectFor` read out of the C++ header: each driver's replacements and conflict flag."""
    source = MIGRATOR_HPP.read_text(encoding="utf-8")
    body = source[source.index("inline Dialect DialectFor") :]
    body = body[: body.index("\n}")]

    arms = re.findall(r"case Driver::(\w+):\s*(?:// [^\n]*\n\s*)?return \{(.+?)\};", body, re.S)
    dialects = {}
    for arm, values in arms:
        literals = re.findall(r'"([^"]*)"', values)
        flag = re.search(r"\b(true|false)\s*$", values.strip())
        assert flag, f"no NeedsConflictClause in the {arm} arm"
        dialects[CPP_DRIVER_NAMES[arm]] = (
            dict(zip(MEMBERS, literals, strict=True)),
            flag.group(1) == "true",
        )
    return dialects


def test_python_and_cpp_dialect_tables_agree():
    assert {driver: text for driver, (text, _) in cpp_dialects().items()} == DIALECTS


def test_python_and_cpp_agree_on_the_conflict_clause():
    sql = "@INSERT_IF_ABSENT@ t (a, b) VALUES (1, 2) @ON_CONFLICT(a, b)@"
    rendered = {driver: resolve_placeholders(sql, driver) for driver in DRIVERS}

    renders_clause = {driver: "CONFLICT" in text for driver, text in rendered.items()}
    assert renders_clause == {driver: flag for driver, (_, flag) in cpp_dialects().items()}
    assert rendered["postgres"].endswith("ON CONFLICT (a, b) DO NOTHING")


def test_an_at_sign_that_is_not_a_placeholder_is_text():
    sql = "VALUES ('someone@example.com'), @TRUE@"
    assert resolve_placeholders(sql, "sqlite") == "VALUES ('someone@example.com'), 1"
    assert resolve_placeholders("a @ b @ c @(x)@", "postgres") == "a @ b @ c @(x)@"


def test_an_unknown_placeholder_is_refused():
    with pytest.raises(VoltmodError):
        resolve_placeholders("id @MADE_UP@", "postgres")


def test_migrations_render_in_version_order(tmp_path: Path):
    (tmp_path / "0002_second.sql").write_text("CREATE TABLE b (id @ID@);\n", encoding="utf-8")
    (tmp_path / "0001_first.sql").write_text("CREATE TABLE a (id @ID@);\n", encoding="utf-8")
    (tmp_path / "notes.sql.bak").write_text("ignored", encoding="utf-8")

    rendered = render_migrations(tmp_path, "sqlite")
    assert rendered.index("TABLE a") < rendered.index("TABLE b")
    assert "ignored" not in rendered


def test_a_dropped_column_leaves_its_create_table():
    ddl = (
        "CREATE TABLE IF NOT EXISTS admins (\n"
        "  id BIGINT,\n"
        "  language VARCHAR(5) NOT NULL DEFAULT 'en',\n"
        "  name TEXT,\n"
        "  last TEXT\n"
        ");\n"
        "ALTER TABLE admins DROP COLUMN language;\n"
        "ALTER TABLE admins DROP COLUMN last;\n"
    )
    assert (
        apply_altered_columns(ddl)
        == "CREATE TABLE IF NOT EXISTS admins (\n  id BIGINT,\n  name TEXT\n);\n"
    )


def test_an_added_column_joins_its_create_table_in_file_order():
    ddl = (
        "CREATE TABLE t (\n"
        "  id BIGINT,\n"
        "  flags TEXT\n"
        ");\n"
        "ALTER TABLE t ADD COLUMN permissions TEXT NOT NULL DEFAULT '[]';\n"
        "UPDATE t SET permissions = '';\n"
        "ALTER TABLE t DROP COLUMN flags;\n"
    )
    assert apply_altered_columns(ddl) == (
        "CREATE TABLE t (\n  id BIGINT,\n  permissions TEXT NOT NULL DEFAULT '[]'\n);\n"
        "UPDATE t SET permissions = '';\n"
    )


def test_dropping_a_column_the_table_lacks_is_refused():
    with pytest.raises(VoltmodError):
        apply_altered_columns("CREATE TABLE t (\n  a INT\n);\nALTER TABLE t DROP COLUMN b;\n")
