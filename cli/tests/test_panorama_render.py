"""Cover the Panorama renderer against the framework's real block library."""

import pytest

from voltmod.errors import VoltmodError
from voltmod.panorama.render import render_screens


def test_a_screen_renders_its_layout_styles_icons_and_header(make_screen_project):
    root = make_screen_project()
    written = render_screens(root, ["ui-lab"])

    out = root / "build/panorama/ui-lab/panorama"
    xml = (out / "layout/custom_game/hud.xml").read_text(encoding="utf-8")
    css = (out / "styles/custom_game/hud.css").read_text(encoding="utf-8")

    assert set(written) == {
        out / "layout/custom_game/hud.xml",
        out / "styles/custom_game/hud.css",
        out / "images/custom_game/weapons/ak47.png",
        out / "images/custom_game/weapons/ak47.vtex",
        root / "build/panorama/ui-lab/include/Ui/Hud.hpp",
    }
    assert 'id="hud_slot0"' in xml and 'id="hud_slot0_button"' in xml
    assert "{s:slot0_label}" in xml
    assert 'src="s2r://panorama/images/custom_game/weapons/ak47.vtex"' in xml
    assert ".Row-button" in css and "color: #e8e6e0;" in css
    assert ".Icon--ak47 .Icon--ak47 {\n  visibility: visible;\n}" in css
    vtex = (out / "images/custom_game/weapons/ak47.vtex").read_text(encoding="utf-8")
    assert vtex.startswith("<!-- dmx encoding")
    assert '"panorama/images/custom_game/weapons/ak47.png"' in vtex


HUD_BLOCKS_XML = """{% import "card.xml.j2" as cards %}
{% import "toast.xml.j2" as toasts %}
<root>
  <Panel id="{{screen}}" class="Screen">
    {{ cards.card("card0", icon_set="weapons", bar=true) }}
    {{ toasts.toast("toast") }}
  </Panel>
</root>
"""

HUD_BLOCKS_CSS = """{% import "bar.css.j2" as bar %}
{% include "card.css.j2" %}
{% include "toast.css.j2" %}
{{ bar.fill_rules("Bar", 4) }}
"""


def test_the_hud_blocks_render(make_screen_project):
    root = make_screen_project(xml=HUD_BLOCKS_XML, css=HUD_BLOCKS_CSS)
    render_screens(root, ["ui-lab"])

    out = root / "build/panorama/ui-lab/panorama"
    xml = (out / "layout/custom_game/hud.xml").read_text(encoding="utf-8")
    css = (out / "styles/custom_game/hud.css").read_text(encoding="utf-8")

    assert 'id="hud_card0_bar"' in xml and 'id="hud_toast"' in xml
    assert "{s:card0_title}" in xml
    assert ".Bar.Step--4 .Bar-fill {\n  width: 100.0%;\n}" in css


def test_an_unknown_token_names_the_file_and_the_token(make_screen_project):
    root = make_screen_project(xml="<root>{{ nonesuch }}</root>")
    with pytest.raises(VoltmodError) as error:
        render_screens(root, [])
    assert "hud.xml.j2" in str(error.value) and "nonesuch" in str(error.value)


def test_a_second_render_writes_nothing(make_screen_project):
    root = make_screen_project()
    assert render_screens(root, [])
    assert render_screens(root, []) == []
