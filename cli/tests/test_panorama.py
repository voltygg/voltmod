"""Cover the Panorama renderer against small hand-built projects.

Nothing rendered is committed, so a regression here ships as a layout that renders nothing.
Most cases run against the framework's real `panorama/`, so a broken block fails here.
"""

from pathlib import Path

import pytest
from voltmod.builder.panorama import compile as compiler
from voltmod.builder.panorama import find_owners, render

KIT = Path(__file__).resolve().parents[2]

#: A 1x1 transparent PNG, so an icon set has a real file in it.
PNG = bytes.fromhex(
    "89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c4"
    "890000000d4944415478da63f8cf0c000203010049810180a4a98c2100000000"
    "49454e44ae426082"
)

HUD_XML = """{% import "card.xml.j2" as blocks %}
<root>
  <Panel class="Layer" hittest="false">
    <Panel id="{{screen}}" class="Screen" hittest="false">
      {{ blocks.card("slot0", icon_set="weapons", bar=true, accent=true) }}
    </Panel>
  </Panel>
</root>
"""

#: The least a screen can be and still bind: an id to name it, a class the driver toggles.
BARE_SCREEN = '<root><Panel id="{{screen}}" class="Screen Hidden" /></root>'

HUD_CSS = """{% import "bar.css" as bar %}
.Screen {
  color: #e8e6e0;
  background-color: rgba(255, 255, 255, 0.05);
}
{% include "card.css" %}
{{ bar.fill_rules("Bar-fill", 4) }}
"""


def plugin(root: Path, xml: str = HUD_XML, css: str = HUD_CSS, name: str = "hud") -> Path:
    """A consumer repo with one plugin that owns one screen and one icon set."""
    panorama = root / "plugins/ui-lab/panorama"
    (panorama / "screens").mkdir(parents=True, exist_ok=True)
    (panorama / "screens" / f"{name}.xml.j2").write_text(xml, encoding="utf-8")
    (panorama / "screens" / f"{name}.css").write_text(css, encoding="utf-8")

    weapons = panorama / "images/custom_game/weapons"
    weapons.mkdir(parents=True, exist_ok=True)
    (weapons / "ak47.png").write_bytes(PNG)
    return root


def fake_kit(path: Path, screen: str = "") -> Path:
    """A framework tree with, when asked, one screen of its own."""
    (path / "panorama").mkdir(parents=True, exist_ok=True)
    if screen:
        (path / "panorama/screens").mkdir(exist_ok=True)
        (path / "panorama/screens/frame.xml.j2").write_text(screen, encoding="utf-8")
        (path / "panorama/screens/frame.css").write_text("", encoding="utf-8")
    return path


def rendered(root: Path, owner: str = "ui-lab") -> Path:
    return root / "build/panorama" / owner / "panorama"


def fails(root: Path, kit: Path) -> str:
    with pytest.raises(SystemExit) as error:
        render.render(root, kit, [])
    return str(error.value)


def test_plugin_screen_renders_layout_styles_and_icons(tmp_path):
    written = render.render(plugin(tmp_path), KIT, ["ui-lab"])

    out = rendered(tmp_path)
    xml = (out / "layout/custom_game/hud.xml").read_text(encoding="utf-8")
    css = (out / "styles/custom_game/hud.css").read_text(encoding="utf-8")

    assert set(written) == {
        out / "layout/custom_game/hud.xml",
        out / "styles/custom_game/hud.css",
        out / "images/custom_game/weapons/ak47.png",
        out / "images/custom_game/weapons/ak47.vtex",
        tmp_path / "build/panorama/ui-lab/include/Ui/Hud.hpp",
    }
    assert 'id="hud_slot0"' in xml and 'id="hud_slot0_bar"' in xml
    assert "{s:slot0_title}" in xml
    assert 'src="s2r://panorama/images/custom_game/weapons/ak47.vtex"' in xml
    assert "Do not edit" in xml.splitlines()[0]
    assert ".Card-title" in css and "color: #e8e6e0;" in css
    assert ".Bar-fill.Step--4 {\n  width: 100.0%;\n}" in css
    vtex = (out / "images/custom_game/weapons/ak47.vtex").read_text(encoding="utf-8")
    assert vtex.startswith("<!-- dmx encoding")
    assert '"panorama/images/custom_game/weapons/ak47.png"' in vtex


def test_framework_menu_carries_the_driver_ids(tmp_path):
    render.render(tmp_path, KIT, ["voltmod"])

    xml = (rendered(tmp_path, "voltmod") / "layout/custom_game/voltmod_menu.xml").read_text(
        encoding="utf-8"
    )
    assert 'id="voltmod_menu_row9_btn"' in xml
    assert 'id="voltmod_menu_nav7"' in xml
    assert "{s:row0_label}" in xml
    css = (rendered(tmp_path, "voltmod") / "styles/custom_game/voltmod_menu.css").read_text(
        encoding="utf-8"
    )
    assert "{{" not in css and "rgba(12, 13, 16, 0.96)" in css


def test_unknown_token_names_the_file_and_the_token(tmp_path):
    root = plugin(tmp_path, xml="<root>{{ nonesuch }}</root>")
    message = fails(root, KIT)
    assert "hud.xml.j2" in message and "nonesuch" in message


def test_skin_is_appended_to_the_screen_stylesheet(tmp_path):
    kit = fake_kit(tmp_path / "kit")
    root = plugin(tmp_path / "repo", xml=BARE_SCREEN, css=".Screen {\n  width: 100%;\n}\n")
    (root / "panorama/skin").mkdir(parents=True, exist_ok=True)
    (root / "panorama/skin/hud.css").write_text(
        ".Screen {\n  color: #abcdef;\n}\n", encoding="utf-8"
    )

    render.render(root, kit, [])

    css = (rendered(root) / "styles/custom_game/hud.css").read_text(encoding="utf-8")
    assert "width: 100%;" in css and "color: #abcdef;" in css


def test_the_framework_owns_its_own_screens_beside_the_plugins(tmp_path):
    kit = fake_kit(tmp_path / "kit", screen="<root>{{screen}}</root>")
    root = plugin(tmp_path / "repo", xml=BARE_SCREEN, css="")

    owners = find_owners(root, kit)

    assert set(owners) == {"voltmod", "ui-lab"}
    assert owners["voltmod"].source == kit / "panorama"
    render.render(root, kit, [])
    assert (rendered(root, "voltmod") / "layout/custom_game/frame.xml").is_file()


def test_second_render_writes_nothing(tmp_path):
    root = plugin(tmp_path)
    assert render.render(root, KIT, [])
    assert render.render(root, KIT, []) == []


def test_unknown_owner_lists_the_known_ones(tmp_path):
    with pytest.raises(SystemExit) as error:
        render.render(plugin(tmp_path), KIT, ["nope"])
    assert "voltmod" in str(error.value) and "ui-lab" in str(error.value)


def test_publish_copies_the_rendered_tree(tmp_path):
    root = plugin(tmp_path)
    render.render(root, KIT, ["ui-lab"])

    directory = tmp_path / "addon"
    count = compiler.publish(root, KIT, ["ui-lab"], directory)

    assert count == 4
    assert (directory / "panorama/layout/custom_game/hud.xml").is_file()
    assert (directory / "panorama/styles/custom_game/hud.css").is_file()
    assert (directory / "panorama/images/custom_game/weapons/ak47.vtex").is_file()
