"""The templates/ and panorama/ trees voltmod ships, and the Jinja templates in them."""

from functools import cache
from pathlib import Path

from jinja2 import Environment, FileSystemLoader, StrictUndefined, Template, select_autoescape

_PACKAGE_DIR = Path(__file__).resolve().parent

# A wheel carries templates/ and panorama/ under bundled/; a checkout keeps them at the repo root.
BUNDLED_DIR = (
    _PACKAGE_DIR / "bundled" if (_PACKAGE_DIR / "bundled").is_dir() else _PACKAGE_DIR.parents[1]
)
TEMPLATES_DIR = BUNDLED_DIR / "templates"


def load_template(name: str) -> Template:
    """One of the bundled Jinja templates, such as `panorama/screen.hpp.j2`."""
    return _template_environment().get_template(name)


@cache
def _template_environment() -> Environment:
    # utf-8-sig: an editor's byte order mark must not reach the output.
    return Environment(
        loader=FileSystemLoader(TEMPLATES_DIR, encoding="utf-8-sig"),
        undefined=StrictUndefined,
        keep_trailing_newline=True,
        trim_blocks=True,
        lstrip_blocks=True,
        autoescape=select_autoescape(["html.j2"]),
    )
