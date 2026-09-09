"""Render a screen to a self-contained HTML approximation for a plain browser.

Panorama CSS is not web CSS (docs/custom-ui.md), so this is a rough stand-in for layout work,
not a client: font metrics, the true no-flow default, and anything client-side differ. It costs
neither the Workshop Tools, a compile, nor a client reconnect - open the file and reload it.
"""

import base64
import html
import re
import webbrowser
from pathlib import Path
from xml.etree import ElementTree

from voltmod.tools import die

from . import bind
from .screens import BUILD_DIR, IMAGES_DIR, SUFFIX, Owner, find_owners, sources
from .screens import screen as render_screen

PREVIEW_DIR = "preview"

_FILL_FLOW = re.compile(r"^fill-parent-flow\((\d+)\)$")
#: Unlike bind's, this keeps the declaration body: the preview translates it, not just reads it.
_RULE = re.compile(r"([^{}]+)\{([^{}]*)\}", re.DOTALL)
_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)

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

#: html/body need a real height for a screen's own `height: 100%` chain to resolve against, and
#: flex-by-default on every div is what makes the margin-auto centering below actually center -
#: Panorama positions every panel within its parent regardless of flow, closer to flex than block.
_BASE_CSS = """
html, body { height: 100%; }
div { position: relative; display: flex; flex-direction: row; box-sizing: border-box;
  min-width: 0; }
body { margin: 0; background: #222; }
#pv-panel { position: fixed; top: 0; right: 0; width: 280px; max-height: 100vh; overflow-y: auto;
  background: #111; color: #eee; font: 12px/1.4 monospace; padding: 10px; z-index: 9999; }
#pv-panel h2 { font-size: 13px; margin: 0 0 10px; word-break: break-all; }
#pv-panel label { display: block; margin-bottom: 8px; }
#pv-panel input, #pv-panel select { width: 100%; }
.pv-button { cursor: pointer; }
"""

_SCRIPT = """
function pvVar(name, value) {
  document.querySelectorAll('[data-var="' + name + '"]').forEach(el => el.textContent = value);
}
function pvFlag(id, cls, on) {
  var el = document.getElementById(id);
  if (el) el.classList.toggle(cls, on);
}
function pvFamily(id, prefix, variant) {
  var el = document.getElementById(id);
  if (!el) return;
  var stale = Array.from(el.classList).filter(cls => cls.startsWith(prefix + '--'));
  stale.forEach(cls => el.classList.remove(cls));
  if (variant) el.classList.add(prefix + '--' + variant);
}
// A family control is a panel picker plus a variant picker; moving the panel clears the old one.
var pvLast = {};
function pvPick(prefix) {
  var where = document.getElementById('pv-' + prefix + '-panel').value;
  var variant = document.getElementById('pv-' + prefix + '-variant').value;
  if (pvLast[prefix] && pvLast[prefix] !== where) pvFamily(pvLast[prefix], prefix, '');
  pvLast[prefix] = where;
  pvFamily(where, prefix, variant);
}
"""


def preview(root: Path, kit_root: Path, target: str) -> Path:
    """Write OWNER/SCREEN as an HTML approximation and return where it landed."""
    owner_name, screen_name = _split(target)
    owners = find_owners(root)
    if owner_name not in owners:
        die(f"no panorama sources for {owner_name}\nKnown: {', '.join(sorted(owners))}")
    owner = owners[owner_name]

    names = [source.name.removesuffix(SUFFIX) for source in sources(owner)]
    if screen_name not in names:
        die(f"{owner_name} has no screen '{screen_name}'\nKnown: {', '.join(names)}")

    layout, stylesheet = render_screen(owner, kit_root, screen_name)
    model = bind.read(layout, stylesheet)

    body = "".join(_convert(child, owner) for child in model.tree)
    document = _document(screen_name, body, _translate_css(stylesheet), _sidebar(model))

    out = root / BUILD_DIR / PREVIEW_DIR / f"{screen_name}.html"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(document, encoding="utf-8")
    return out


def open_in_browser(path: Path) -> None:
    """Open a written preview in the default browser."""
    webbrowser.open(path.as_uri())


def _split(target: str) -> tuple[str, str]:
    parts = target.split("/")
    if len(parts) != 2 or not all(parts):
        die(f"'{target}' is not OWNER/SCREEN")
    return parts[0], parts[1]


def _convert(node: ElementTree.Element, owner: Owner) -> str:
    if node.tag == "styles":
        return ""
    if node.tag == "Image":
        return _image(node, owner)
    if node.tag == "Label":
        return f"<div {_attrs(node)}>{_label_text(node)}</div>"
    inner = "".join(_convert(child, owner) for child in node)
    extra = "pv-button" if node.tag == "Button" else ""
    return f"<div {_attrs(node, extra)}>{inner}</div>"


def _attrs(node: ElementTree.Element, extra_class: str = "") -> str:
    bits = []
    identifier = node.get("id")
    if identifier:
        bits.append(f'id="{html.escape(identifier, quote=True)}"')
    classes = " ".join(filter(None, [node.get("class", ""), extra_class])).strip()
    if classes:
        bits.append(f'class="{html.escape(classes, quote=True)}"')
    return " ".join(bits)


def _label_text(node: ElementTree.Element) -> str:
    text = node.get("text", "")
    match = bind.VAR.match(text)
    if not match:
        return html.escape(text)
    name = match.group(1)
    return f'<span data-var="{name}">{html.escape(name)}</span>'


def _image(node: ElementTree.Element, owner: Owner) -> str:
    match = bind.IMAGE_SRC.match(node.get("src", ""))
    src = ""
    if match:
        icon_set, name = match.groups()
        png = owner.source / IMAGES_DIR / icon_set / f"{name}.png"
        if png.is_file():
            data = base64.b64encode(png.read_bytes()).decode("ascii")
            src = f"data:image/png;base64,{data}"
    return f'<img {_attrs(node)} src="{src}">'


def _translate_css(stylesheet: str) -> str:
    cleaned = _COMMENT.sub("", stylesheet)
    rules = []
    for selector, body in _RULE.findall(cleaned):
        selector = selector.strip()
        if not selector:
            continue
        declarations = [
            declaration
            for raw in body.split(";")
            if ":" in raw
            for declaration in _declaration(*raw.split(":", 1))
        ]
        rules.append(selector + " {\n  " + ";\n  ".join(declarations) + ";\n}")
    return "\n".join(rules)


def _declaration(prop: str, value: str) -> list[str]:
    """One Panorama declaration translated into its web-CSS equivalent(s), or dropped."""
    prop, value = prop.strip(), value.strip()

    if prop == "flow-children":
        return ["display: flex", f"flex-direction: {'row' if value == 'right' else 'column'}"]
    if prop == "visibility":
        return ["display: " + ("none" if value == "collapse" else "revert")]
    if prop in ("width", "height"):
        fill = _FILL_FLOW.match(value)
        if fill:
            return [f"flex: {fill.group(1)} 1 0"]
        return [f"{prop}: {'auto' if value == 'fit-children' else value}"]
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


def _sidebar(model: bind.Screen) -> str:
    """Controls for everything the server can write: a variable, a Hidden flag, a class family.

    Which family belongs on which panel is the plugin's business, not the layout's, so a family
    is offered with a panel picker rather than guessed at.
    """
    rows = [_variable_row(name) for name in model.variables]
    panels = [model.name, *model.ids]
    rows += [_flag_row(identifier, "Hidden") for identifier in panels]
    rows += [_family_row(prefix, variants, panels) for prefix, variants in model.families.items()]
    return "\n".join(rows)


def _variable_row(name: str) -> str:
    return (
        f"<label>{html.escape(name)}<br>"
        f'<input value="{html.escape(name)}" oninput="pvVar({_js(name)}, this.value)"></label>'
    )


def _flag_row(identifier: str, cls: str) -> str:
    return (
        f'<label><input type="checkbox" '
        f'onchange="pvFlag({_js(identifier)}, {_js(cls)}, this.checked)"> '
        f"{html.escape(identifier)}.{html.escape(cls)}</label>"
    )


def _family_row(prefix: str, variants: list[str], panels: list[str]) -> str:
    """One family: which panel to write it on, and which variant to put there."""
    where = _options(panels)
    variant = _options(variants, none=True)
    pick = f"pvPick({_js(prefix)})"
    return (
        f"<label>{html.escape(prefix)}<br>"
        f'<select id="pv-{prefix}-panel" onchange="{pick}">{where}</select>'
        f'<select id="pv-{prefix}-variant" onchange="{pick}">{variant}</select></label>'
    )


def _options(values: list[str], none: bool = False) -> str:
    head = '<option value="">none</option>' if none else ""
    return head + "".join(
        f'<option value="{html.escape(value)}">{html.escape(value)}</option>' for value in values
    )


def _js(text: str) -> str:
    """A plain id/class/var name as a single-quoted JS string literal."""
    return "'" + text.replace("\\", "\\\\").replace("'", "\\'") + "'"


def _document(name: str, body: str, css: str, sidebar: str) -> str:
    title = html.escape(name)
    return (
        "<!doctype html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n"
        f"<title>{title} preview</title>\n"
        f"<style>\n{_BASE_CSS}\n{css}\n</style>\n</head>\n<body>\n{body}\n"
        f'<aside id="pv-panel">\n<h2>{title}</h2>\n{sidebar}\n</aside>\n'
        f"<script>\n{_SCRIPT}\n</script>\n</body>\n</html>\n"
    )
