import re
import subprocess
import sys
import tempfile
from pathlib import Path

from voltmod.bundled import TEMPLATES_DIR
from voltmod.database.migrations import apply_altered_columns
from voltmod.errors import VoltmodError

TABLE_HEADER_TEMPLATE = TEMPLATES_DIR / "database/table-header.in"


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
