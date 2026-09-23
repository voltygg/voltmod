import re
import sys
import tempfile
from pathlib import Path

from voltmod.bundled import TEMPLATES_DIR
from voltmod.database.migrations import apply_altered_columns
from voltmod.errors import VoltmodError
from voltmod.toolchain.process import run

TABLE_HEADER_TEMPLATE = TEMPLATES_DIR / "database/table-header.in"


def generate_table_header(root: Path, ddl: str, namespace: str, header_name: str) -> str:
    """Run sqlpp23-ddl2cpp over `ddl` in a temporary directory, and return the header it wrote."""
    with tempfile.TemporaryDirectory() as work:
        source = Path(work) / "schema.sql"
        source.write_text(apply_altered_columns(ddl), encoding="utf-8", newline="\n")
        target = Path(work) / header_name
        options: dict[str, str | Path] = {
            "--path-to-ddl": source,
            "--path-to-header": target,
            "--namespace": namespace,
            "--naming-style": "camel-case",
            "--path-to-custom-template": TABLE_HEADER_TEMPLATE,
        }
        arguments = [part for option in options.items() for part in option]
        run(sys.executable, _find_ddl2cpp(root), *arguments, "--assume-auto-id", cwd=root)
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
