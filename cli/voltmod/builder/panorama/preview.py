"""Render a screen to a self-contained HTML approximation for a plain browser.

Panorama CSS is not web CSS (docs/custom-ui.md), so this is a rough stand-in for layout work, not
a client: font metrics, the true no-flow default, and anything client-side differ. It costs
neither the Workshop Tools, a compile, nor a client reconnect - open the file and reload it.

The page shell, its base stylesheet and the script driving the side panel live in
`panorama/preview.html.in`. Everything here fills four slots in it: the screen's markup, its
translated stylesheet, the controls, and the title.
"""

import base64
import html
import re
import webbrowser
from pathlib import Path
from xml.etree import ElementTree

from voltmod.tools import die

from . import bind, screens
from .screens import BUILD_DIR, Owner

PREVIEW_DIR = "preview"
SHELL = "panorama/preview.html.in"

#: Passed through verbatim regardless of value; the exhaustive list from the preview spec.
_PASSTHROUGH = {
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
_PASSTHROUGH_PREFIXES = ("margin", "padding", "border", "transition-")

_FILL_FLOW = re.compile(r"^fill-parent-flow\((\d+)\)$")


def preview(root: Path, kit_root: Path, target: str) -> Path:
    """Write OWNER/SCREEN as an HTML approximation and return where it landed."""
    owner, name = _resolve(root, target)
    layout, stylesheet = screens.screen(owner, kit_root, name)
    screen = bind.read(layout, stylesheet)

    page = (kit_root / SHELL).read_text(encoding="utf-8")
    for slot, value in [
        ("%CSS%", _translate_css(stylesheet)),
        ("%BODY%", "".join(_convert(child, owner) for child in screen.tree)),
        ("%CONTROLS%", _controls(screen)),
        ("%TITLE%", html.escape(name)),
    ]:
        page = page.replace(slot, value)

    out = root / BUILD_DIR / PREVIEW_DIR / f"{name}.html"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(page, encoding="utf-8")
    return out


def open_in_browser(path: Path) -> None:
    """Open a written preview in the default browser."""
    webbrowser.open(path.as_uri())


def _resolve(root: Path, target: str) -> tuple[Owner, str]:
    """`OWNER/SCREEN` as the owner that ships it and the screen's name."""
    parts = target.split("/")
    if len(parts) != 2 or not all(parts):
        die(f"'{target}' is not OWNER/SCREEN")

    owner = screens.select(screens.find_owners(root), [parts[0]])[parts[0]]

    names = [screens.stem(source) for source in screens.sources(owner)]
    if parts[1] not in names:
        die(f"{parts[0]} has no screen '{parts[1]}'\nKnown: {', '.join(names) or 'none'}")
    return owner, parts[1]


def _convert(node: ElementTree.Element, owner: Owner) -> str:
    """One layout node as a div, an img, or nothing. Panels nest, so this recurses."""
    if node.tag == "styles":
        return ""
    if node.tag == "Image":
        return f'<img {_attrs(node)} src="{_image(node, owner)}">'
    if node.tag == "Label":
        return f"<div {_attrs(node)}>{_label(node)}</div>"

    inner = "".join(_convert(child, owner) for child in node)
    return f"<div {_attrs(node, 'pv-button' if node.tag == 'Button' else '')}>{inner}</div>"


def _attrs(node: ElementTree.Element, extra_class: str = "") -> str:
    parts = []
    if identifier := node.get("id"):
        parts.append(f'id="{html.escape(identifier, quote=True)}"')
    if classes := " ".join(filter(None, [node.get("class", ""), extra_class])).strip():
        parts.append(f'class="{html.escape(classes, quote=True)}"')
    return " ".join(parts)


def _label(node: ElementTree.Element) -> str:
    """A Label's text, or the dialog variable it reads, tagged so a control can drive it."""
    match = bind.VAR.match(node.get("text", ""))
    if not match:
        return html.escape(node.get("text", ""))
    name = match.group(1)
    return f'<span data-var="{html.escape(name, quote=True)}">{html.escape(name)}</span>'


def _image(node: ElementTree.Element, owner: Owner) -> str:
    """The referenced PNG inlined as a data URI, so the page needs no files beside it."""
    match = bind.IMAGE_SRC.match(node.get("src", ""))
    if not match:
        return ""
    png = screens.icon(owner, *match.groups())
    if not png.is_file():
        return ""
    return f"data:image/png;base64,{base64.b64encode(png.read_bytes()).decode('ascii')}"


def _translate_css(stylesheet: str) -> str:
    rules = []
    for selector, body in bind.rules(stylesheet):
        declarations = [
            translated
            for raw in body.split(";")
            if ":" in raw
            for translated in _declaration(*raw.split(":", 1))
        ]
        rules.append(f"{selector} {{\n  " + ";\n  ".join(declarations) + ";\n}")
    return "\n".join(rules)


def _declaration(prop: str, value: str) -> list[str]:
    """One Panorama declaration as its web-CSS equivalent(s), or a note saying it was dropped."""
    prop, value = prop.strip(), value.strip()

    if prop == "flow-children":
        return ["display: flex", f"flex-direction: {'row' if value == 'right' else 'column'}"]
    if prop == "visibility":
        return ["display: " + ("none" if value == "collapse" else "revert")]
    if prop in ("width", "height"):
        if fill := _FILL_FLOW.match(value):
            return [f"flex: {fill.group(1)} 1 0"]
        if value == "fit-children":
            return [f"{prop}: auto"]
        # Panorama lays children out inside an explicit size; a flex item would shrink below it.
        return [f"{prop}: {value}", "flex-shrink: 0"]
    if prop == "horizontal-align":
        return _align(value, "left", "margin-right", "margin-left")
    if prop == "vertical-align":
        return _align(value, "top", "margin-bottom", "margin-top")
    if prop in _PASSTHROUGH or prop.startswith(_PASSTHROUGH_PREFIXES):
        return [f"{prop}: {value}"]
    return [f"/* dropped: {prop} */"]


def _align(value: str, near: str, near_margin: str, far_margin: str) -> list[str]:
    """`center` pushes both auto margins; a near/far edge pushes only the margin opposite it."""
    if value == "center":
        return [f"{near_margin}: auto", f"{far_margin}: auto"]
    return [f"{far_margin}: auto"] if value == near else [f"{near_margin}: auto"]


def _controls(screen: bind.Screen) -> str:
    """A control for everything the server can write: a variable, a Hidden flag, a class family.

    Which family belongs on which panel is the plugin's business, not the layout's, so a family
    is offered with a panel picker rather than guessed at. Each control names what it drives in a
    data attribute and the shell's script wires the behaviour up, so nothing here has to escape a
    name into a JavaScript literal.
    """
    panels = [screen.name, *screen.ids]
    rows = [_variable(name) for name in screen.variables]
    rows += [_family(name, variants, panels) for name, variants in screen.families.items()]

    # One per panel is a long list on a real screen, so it starts folded away.
    flags = "\n".join(_flag(panel) for panel in panels)
    rows.append(f"<details><summary>Hidden ({len(panels)})</summary>\n{flags}\n</details>")
    return "\n".join(rows)


def _variable(name: str) -> str:
    quoted = html.escape(name, quote=True)
    return f'<label>{html.escape(name)}<br><input data-pv-var="{quoted}" value="{quoted}"></label>'


def _flag(panel: str) -> str:
    return (
        f'<label><input type="checkbox" data-pv-flag="Hidden" '
        f'data-pv-id="{html.escape(panel, quote=True)}"> {html.escape(panel)}.Hidden</label>'
    )


def _family(name: str, variants: list[str], panels: list[str]) -> str:
    quoted = html.escape(name, quote=True)
    return (
        f"<label>{html.escape(name)}<br>"
        f'<select data-pv-family="{quoted}" data-pv-role="panel">{_options(panels)}</select>'
        f'<select data-pv-family="{quoted}" data-pv-role="variant">'
        f"{_options(variants, none=True)}</select></label>"
    )


def _options(values: list[str], none: bool = False) -> str:
    head = '<option value="">none</option>' if none else ""
    return head + "".join(
        f'<option value="{html.escape(value, quote=True)}">{html.escape(value)}</option>'
        for value in values
    )
