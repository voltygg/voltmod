"""Cover the Panorama renderer against small hand-built projects.

Nothing rendered is committed, so a regression here ships as a layout that renders nothing.
Every case runs against the framework's real `panorama/blocks/`, so a broken block fails here.
"""

from pathlib import Path

import pytest
from voltmod.builder.panorama import compile as compiler
from voltmod.builder.panorama import screens

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


def rendered(root: Path, owner: str = "ui-lab") -> Path:
    return root / "build/panorama" / owner / "panorama"


def fails(root: Path, kit: Path) -> str:
    with pytest.raises(SystemExit) as error:
        screens.render(root, kit, [])
    return str(error.value)


def test_plugin_screen_renders_layout_styles_and_icons(tmp_path):
    written = screens.render(plugin(tmp_path), KIT, ["ui-lab"])

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
    assert ".Card-title" in css and "color: #e8e6e0;" in css
    assert ".Bar-fill.Step--4 {\n  width: 100.0%;\n}" in css
    vtex = (out / "images/custom_game/weapons/ak47.vtex").read_text(encoding="utf-8")
    assert vtex.startswith("<!-- dmx encoding")
    assert '"panorama/images/custom_game/weapons/ak47.png"' in vtex


def test_unknown_token_names_the_file_and_the_token(tmp_path):
    root = plugin(tmp_path, xml="<root>{{ nonesuch }}</root>")
    message = fails(root, KIT)
    assert "hud.xml.j2" in message and "nonesuch" in message


def test_the_framework_is_not_an_owner(tmp_path):
    """It ships blocks, which templates import; it owns no screen tree of its own."""
    owners = screens.find_owners(plugin(tmp_path))

    assert set(owners) == {"ui-lab"}
    assert not (KIT / "panorama/screens").exists()


def test_second_render_writes_nothing(tmp_path):
    root = plugin(tmp_path)
    assert screens.render(root, KIT, [])
    assert screens.render(root, KIT, []) == []


def test_unknown_owner_lists_the_known_ones(tmp_path):
    with pytest.raises(SystemExit) as error:
        screens.render(plugin(tmp_path), KIT, ["nope"])
    assert "nope" in str(error.value) and "ui-lab" in str(error.value)


def test_publish_copies_the_rendered_tree(tmp_path):
    root = plugin(tmp_path)
    screens.render(root, KIT, ["ui-lab"])

    directory = tmp_path / "addon"
    count = compiler.publish(root, ["ui-lab"], directory)

    assert count == 4
    assert (directory / "panorama/layout/custom_game/hud.xml").is_file()
    assert (directory / "panorama/styles/custom_game/hud.css").is_file()
    assert (directory / "panorama/images/custom_game/weapons/ak47.vtex").is_file()
