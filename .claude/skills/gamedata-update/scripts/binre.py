"""Run a snippet with the gamedata-update helpers in scope, from the framework checkout:

    uv run --with capstone --with numpy python \
        .claude/skills/gamedata-update/scripts/binre.py snippet.py

    # snippet.py
    new = Binary.open("2000913", "windows")  # archived by `voltmod framework gamedata fetch`
    print(new.disasm(new.find("48 89 5C 24 08")[0]))
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "cli"))

from analysis import (  # noqa: E402
    drift,
    load_gamedata,
    load_resolved,
    make_pattern,
    print_slots,
    slots,
    vtable,
)
from binary import Binary, Function, rip_target  # noqa: E402

# What a snippet can use without importing it.
__all__ = [
    "Binary",
    "Function",
    "drift",
    "load_gamedata",
    "load_resolved",
    "make_pattern",
    "print_slots",
    "rip_target",
    "slots",
    "vtable",
]

if __name__ == "__main__":
    snippet = Path(sys.argv[1])
    exec(compile(snippet.read_text(encoding="utf-8"), str(snippet), "exec"), globals())
