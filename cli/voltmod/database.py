"""Rendering migrations for each database driver, and generating their sqlpp23 table header."""

import re
import subprocess
import sys
import tempfile
from pathlib import Path

from voltmod.bundled import TEMPLATES_DIR
from voltmod.errors import VoltmodError

# Mirrors DialectFor in include/VoltMod/Database/Migrator.hpp; test_database.py fails on drift.
# @ON_CONFLICT(columns)@ is resolved in code: only Postgres renders a clause for it.
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

TABLE_HEADER_TEMPLATE = TEMPLATES_DIR / "database/table-header.in"

_PLACEHOLDER = re.compile(r"@([A-Z_]+(?:\([^)@]*\))?)@")
_CONFLICT_PREFIX = "ON_CONFLICT("
_ALTER_COLUMN = re.compile(
    r"^\s*ALTER\s+TABLE\s+(\w+)\s+(ADD|DROP)\s+COLUMN\s+(?:IF\s+(?:NOT\s+)?EXISTS\s+)?(\w+)([^;]*);[^\S\n]*\n?",
    re.I | re.M,
)


def resolve_placeholders(sql: str, driver: str) -> str:
    """Substitute every @PLACEHOLDER@ in `sql` for `driver`; an unknown one is an error."""
    if driver not in DIALECTS:
        raise VoltmodError(f"unknown driver '{driver}'; expected {', '.join(DRIVERS)}")
    values = DIALECTS[driver]

    def replace(match: re.Match[str]) -> str:
        name = match.group(1)
        if name.startswith(_CONFLICT_PREFIX):
            columns = name.removeprefix(_CONFLICT_PREFIX).removesuffix(")")
            return f"ON CONFLICT ({columns}) DO NOTHING" if driver == "postgres" else ""
        if name not in values:
            raise VoltmodError(f"unknown migration placeholder @{name}@")
        return values[name]

    return _PLACEHOLDER.sub(replace, sql)


def find_migrations(directory: Path) -> list[Path]:
    """Every `NNNN_*.sql` in `directory`, in version order."""
    numbered = []
    for path in directory.glob("*.sql"):
        if match := re.match(r"(\d+)", path.name):
            numbered.append((int(match.group(1)), path))
    if not numbered:
        raise VoltmodError(f"no NNNN_*.sql migrations in {directory}")
    return [path for _, path in sorted(numbered)]


def render_migrations(source: Path, driver: str) -> str:
    """One SQL file, or a whole migration directory in version order, rendered for `driver`."""
    files = [source] if source.is_file() else find_migrations(source)
    rendered = (resolve_placeholders(path.read_text(encoding="utf-8"), driver) for path in files)
    return "\n".join(rendered)


def apply_altered_columns(ddl: str) -> str:
    """Fold each `ALTER TABLE t ADD|DROP COLUMN` into t's CREATE TABLE in file order, for ddl2cpp."""
    for table, action, column, definition in _ALTER_COLUMN.findall(ddl):
        create = re.search(
            rf"CREATE\s+TABLE\s+(?:IF\s+NOT\s+EXISTS\s+)?{table}\s*\((.*?)\n\);", ddl, re.I | re.S
        )
        if not create:
            raise VoltmodError(
                f"{action} COLUMN {column} names a table with no CREATE TABLE: {table}"
            )
        if action.upper() == "ADD":
            body = f"{create.group(1).rstrip()},\n  {column}{definition.rstrip()}"
        else:
            body = re.sub(
                rf"^\s*{column}\s[^\n]*\n?", "", create.group(1), count=1, flags=re.I | re.M
            )
            if body == create.group(1):
                raise VoltmodError(f"DROP COLUMN names a column {table} does not have: {column}")
            # The dropped column may have been the last one.
            body = body.rstrip().removesuffix(",")
        ddl = ddl[: create.start(1)] + body + ddl[create.end(1) :]
    return _ALTER_COLUMN.sub("", ddl)


def generate_table_header(root: Path, ddl: str, namespace: str, header_name: str) -> str:
    """Run sqlpp23-ddl2cpp over `ddl` in a temporary directory, and return the header it wrote."""
    with tempfile.TemporaryDirectory() as work:
        source = Path(work) / "schema.sql"
        source.write_text(apply_altered_columns(ddl), encoding="utf-8", newline="\n")
        target = Path(work) / header_name
        # fmt: off
        subprocess.run(
            [
                sys.executable, str(_find_ddl2cpp(root)),
                "--path-to-ddl", str(source),
                "--path-to-header", str(target),
                "--namespace", namespace,
                "--naming-style", "camel-case",
                "--assume-auto-id",
                "--path-to-custom-template", str(TABLE_HEADER_TEMPLATE),
            ],
            check=True,
            cwd=root,
        )
        # fmt: on
        return target.read_text(encoding="utf-8")


def _find_ddl2cpp(root: Path) -> Path:
    # Conan's generated data file is the only record of where the sqlpp23 package lives.
    for data in sorted((root / "build").glob("*/generators/Sqlpp23-*-data.cmake")):
        text = data.read_text(encoding="utf-8")
        if match := re.search(r'set\(sqlpp23_PACKAGE_FOLDER_\w+ "([^"]+)"\)', text):
            script = Path(match.group(1)) / "bin" / "sqlpp23-ddl2cpp"
            if script.is_file():
                return script
    raise VoltmodError(
        "sqlpp23-ddl2cpp not found; run `voltmod build` once so Conan resolves the package"
    )
