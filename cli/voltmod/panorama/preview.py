"""A rough HTML preview of a screen for a plain browser; Panorama CSS is not web CSS."""

import base64
import html
import re
import webbrowser
from pathlib import Path
from xml.etree import ElementTree

from voltmod.errors import VoltmodError
from voltmod.panorama.layout import DIALOG_VARIABLE, IMAGE_SOURCE, css_rules, read_screen
from voltmod.panorama.render import (
    BUILD_DIR,
    ScreenOwner,
    find_screen_owners,
    icon_path,
    render_screen,
    screen_name,
    screen_sources,
    select_owners,
)
from voltmod.project import load_template

PREVIEW_DIR = "preview"

# Passed through whatever the value.
PASSTHROUGH_PROPERTIES = {
    "width",
    "height",
    "background-color",
    "font-size",
    "font-weight",
    "font-style",
    "color",
    "opacity",
    "text-align",
    "text-overflow",
    "transform",
    "border-radius",
}
PASSTHROUGH_PREFIXES = ("margin", "padding", "border", "transition-")

_FILL_PARENT_FLOW = re.compile(r"^fill-parent-flow\((\d+)\)$")


def write_preview(root: Path, target: str) -> Path:
    """Write OWNER/SCREEN as a self-contained HTML page, and return where it landed."""
    owner, name = _find_screen(root, target)
    layout, stylesheet = render_screen(owner, name)
    screen = read_screen(layout, stylesheet)

    page = load_template("panorama/preview.html.j2").render(
        title=name,
        css=_translate_css(stylesheet),
        body="".join(_to_html(child, owner) for child in screen.tree),
        variables=screen.variables,
        families=screen.families,
        panels=[screen.name, *screen.ids],
    )

    out = root / BUILD_DIR / PREVIEW_DIR / f"{name}.html"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(page, encoding="utf-8")
    return out


def open_in_browser(path: Path) -> None:
    webbrowser.open(path.as_uri())


def _find_screen(root: Path, target: str) -> tuple[ScreenOwner, str]:
    parts = target.split("/")
    if len(parts) != 2 or not all(parts):
        raise VoltmodError(f"'{target}' is not OWNER/SCREEN")

    owner_name, name = parts
    owner = select_owners(find_screen_owners(root), [owner_name])[owner_name]
    known = [screen_name(source) for source in screen_sources(owner)]
    if name not in known:
        raise VoltmodError(
            f"{owner_name} has no screen '{name}'\nKnown: {', '.join(known) or 'none'}"
        )
    return owner, name


def _to_html(node: ElementTree.Element, owner: ScreenOwner) -> str:
    """One layout node as a div, an img, or nothing; panels nest, so this recurses."""
    if node.tag == "styles":
        return ""
    if node.tag == "Image":
        return f'<img {_html_attributes(node)} src="{_image_data_uri(node, owner)}">'
    if node.tag == "Label":
        return f"<div {_html_attributes(node)}>{_label_html(node)}</div>"

    inner = "".join(_to_html(child, owner) for child in node)
    extra_class = "pv-button" if node.tag == "Button" else ""
    return f"<div {_html_attributes(node, extra_class)}>{inner}</div>"


def _html_attributes(node: ElementTree.Element, extra_class: str = "") -> str:
    parts = []
    if identifier := node.get("id"):
        parts.append(f'id="{html.escape(identifier, quote=True)}"')
    if classes := " ".join(filter(None, [node.get("class", ""), extra_class])).strip():
        parts.append(f'class="{html.escape(classes, quote=True)}"')
    return " ".join(parts)


def _label_html(node: ElementTree.Element) -> str:
    """A Label's text, or the dialog variable it reads, tagged so a control can drive it."""
    match = DIALOG_VARIABLE.match(node.get("text", ""))
    if not match:
        return html.escape(node.get("text", ""))
    name = match.group(1)
    return f'<span data-var="{html.escape(name, quote=True)}">{html.escape(name)}</span>'


def _image_data_uri(node: ElementTree.Element, owner: ScreenOwner) -> str:
    """The referenced PNG inlined, so the page needs no files beside it."""
    match = IMAGE_SOURCE.match(node.get("src", ""))
    if not match:
        return ""
    png = icon_path(owner, *match.groups())
    if not png.is_file():
        return ""
    return f"data:image/png;base64,{base64.b64encode(png.read_bytes()).decode('ascii')}"


def _translate_css(stylesheet: str) -> str:
    rules = []
    for selector, body in css_rules(stylesheet):
        declarations = [
            translated
            for raw in body.split(";")
            if ":" in raw
            for translated in _translate_declaration(*raw.split(":", 1))
        ]
        rules.append(f"{selector} {{\n  " + ";\n  ".join(declarations) + ";\n}")
    return "\n".join(rules)


def _translate_declaration(prop: str, value: str) -> list[str]:
    """One Panorama declaration as its web CSS equivalents, or a note that it was dropped."""
    prop, value = prop.strip(), value.strip()

    if prop == "flow-children":
        return ["display: flex", f"flex-direction: {'row' if value == 'right' else 'column'}"]
    if prop == "visibility":
        return ["display: " + ("none" if value == "collapse" else "revert")]
    if prop in ("width", "height"):
        if fill := _FILL_PARENT_FLOW.match(value):
            return [f"flex: {fill.group(1)} 1 0"]
        if value == "fit-children":
            return [f"{prop}: auto"]
        # Panorama lays children out inside an explicit size; a flex item would shrink below it.
        return [f"{prop}: {value}", "flex-shrink: 0"]
    if prop == "horizontal-align":
        return _translate_alignment(value, "left", "margin-right", "margin-left")
    if prop == "vertical-align":
        return _translate_alignment(value, "top", "margin-bottom", "margin-top")
    if prop in PASSTHROUGH_PROPERTIES or prop.startswith(PASSTHROUGH_PREFIXES):
        return [f"{prop}: {value}"]
    return [f"/* dropped: {prop} */"]


def _translate_alignment(value: str, near: str, near_margin: str, far_margin: str) -> list[str]:
    """`center` pushes both auto margins; an edge pushes only the margin opposite it."""
    if value == "center":
        return [f"{near_margin}: auto", f"{far_margin}: auto"]
    return [f"{far_margin}: auto"] if value == near else [f"{near_margin}: auto"]
