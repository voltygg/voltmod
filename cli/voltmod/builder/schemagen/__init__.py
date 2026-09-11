"""Generate C++ schema accessors from a schema dump and manifest.

The runtime writes the dump during schema verification. The generator emits selected classes with
baked-in offsets and a layout table for load-time checks. `model`, `fields`, `accessors`, and `emit`
resolve, describe, and render the output; `command` exposes the CLI.
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
