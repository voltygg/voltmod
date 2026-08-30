"""Generate the C++ schema accessor layer from a schema dump and a manifest.

The dump comes from the `schema_dump` console command (tools/schema-dump). This turns the
classes and fields the manifest names into plain C++ with the offsets baked in, plus the layout
table the load-time verifier compares against the live schema.

The work splits four ways: `model` is the resolved shape of a class and the naming rules,
`fields` maps a dumped schema type onto a member, `accessors` says what one member looks like
in C++, and `emit` says what one file looks like. `command` wires them to the CLI.
"""

from .command import app, generate
from .emit import (
    emit_api,
    emit_class_source,
    emit_enums,
    emit_header,
    emit_layout_source,
    emit_wrapper,
)
from .model import Klass, Member, accessor_name
from .resolve import build_classes, collect_enums, trimmed_dump

__all__ = [
    "Klass",
    "Member",
    "accessor_name",
    "app",
    "build_classes",
    "collect_enums",
    "emit_api",
    "emit_class_source",
    "emit_enums",
    "emit_header",
    "emit_layout_source",
    "emit_wrapper",
    "generate",
    "trimmed_dump",
]
