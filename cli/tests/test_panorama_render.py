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
    assert 'id="hud_slot0"' in xml and "{s:slot0_label}" in xml
    assert 'src="s2r://panorama/images/custom_game/weapons/ak47.vtex"' in xml
    assert ".button__label" in css and "color: #e8e6e0;" in css
    assert ".icon-set--ak47 .icon-set__icon--ak47 {\n  visibility: visible;\n}" in css
    vtex = (out / "images/custom_game/weapons/ak47.vtex").read_text(encoding="utf-8")
    assert vtex.startswith("<!-- dmx encoding")
    assert '"panorama/images/custom_game/weapons/ak47.png"' in vtex


HUD_BLOCKS_XML = """{% import "card.xml.j2" as cards %}
{% import "toast.xml.j2" as toasts %}
<root>
  <Panel id="{{screen}}" class="screen">
    {{ cards.card("card0", icon_set=png_icons("weapons"), bar=true) }}
    {{ toasts.toast("toast") }}
  </Panel>
</root>
"""

HUD_BLOCKS_CSS = """{% import "bar.css.j2" as bar %}
{% include "card.css.j2" %}
{% include "toast.css.j2" %}
{{ bar.styles(4) }}
"""


def test_the_hud_blocks_render(make_screen_project):
    root = make_screen_project(xml=HUD_BLOCKS_XML, css=HUD_BLOCKS_CSS)
    render_screens(root, ["ui-lab"])

    out = root / "build/panorama/ui-lab/panorama"
    xml = (out / "layout/custom_game/hud.xml").read_text(encoding="utf-8")
    css = (out / "styles/custom_game/hud.css").read_text(encoding="utf-8")

    assert 'id="hud_card0_bar"' in xml and 'id="hud_toast"' in xml
    assert "{s:card0_title}" in xml
    assert ".bar.bar--step-4 .bar__fill {\n  width: 100.0%;\n}" in css


def test_an_unknown_token_names_the_file_and_the_token(make_screen_project):
    root = make_screen_project(xml="<root>{{ nonesuch }}</root>")
    with pytest.raises(VoltmodError) as error:
        render_screens(root, [])
    assert "hud.xml.j2" in str(error.value) and "nonesuch" in str(error.value)


def test_a_second_render_writes_nothing(make_screen_project):
    root = make_screen_project()
    assert render_screens(root, [])
    assert render_screens(root, []) == []


def test_a_removed_screen_and_icon_leave_nothing_rendered(make_screen_project):
    root = make_screen_project(name="old", icons=("ak47", "m4a1"))
    render_screens(root, [])
    screens = root / "plugins/ui-lab/panorama/screens"
    (screens / "old.xml.j2").unlink()
    (screens / "old.css.j2").unlink()
    (root / "plugins/ui-lab/panorama/images/weapons/m4a1.png").unlink()
    make_screen_project()

    render_screens(root, [])

    out = root / "build/panorama/ui-lab"
    assert not (out / "panorama/layout/custom_game/old.xml").exists()
    assert not (out / "panorama/styles/custom_game/old.css").exists()
    assert not (out / "panorama/images/custom_game/weapons/m4a1.vtex").exists()
    assert not (out / "include/Ui/Old.hpp").exists()
    assert (out / "panorama/layout/custom_game/hud.xml").is_file()


def importing(make_screen_project, template: str):
    """A screen importing `template`, beside a brand-kit plugin that ships brand/logo.xml.j2."""
    root = make_screen_project(
        xml=f'{{% import "{template}" as brand %}}<root><Panel id="{{{{screen}}}}">'
        "{{ brand.logo() }}</Panel></root>",
        css="",
    )
    templates = root / "plugins/brand-kit/panorama/templates/brand"
    templates.mkdir(parents=True)
    (templates / "logo.xml.j2").write_text(
        '{% macro logo() %}<Label text="MEAT" />{% endmacro %}', encoding="utf-8"
    )
    return root


def test_a_screen_imports_a_template_another_plugin_ships(make_screen_project):
    root = importing(make_screen_project, "@brand-kit/brand/logo.xml.j2")

    render_screens(root, ["ui-lab"])

    xml = (root / "build/panorama/ui-lab/panorama/layout/custom_game/hud.xml").read_text(
        encoding="utf-8"
    )
    assert '<Label text="MEAT" />' in xml


def test_plugin_templates_are_not_found_through_an_ambiguous_bare_path(make_screen_project):
    root = importing(make_screen_project, "brand/logo.xml.j2")

    with pytest.raises(VoltmodError, match="brand/logo.xml.j2"):
        render_screens(root, ["ui-lab"])


def test_the_menu_block_draws_the_ids_panorama_menu_screen_writes(make_screen_project):
    root = make_screen_project(
        xml='{% extends "screen.xml.j2" %}{% import "menu.xml.j2" as blocks %}{% block content %}'
        '{% call blocks.menu(2, 2, png_icons("weapons")) %}<Label text="{s:brand}" />{% endcall %}'
        "{% endblock %}",
        css='{% include "menu.css.j2" %}',
    )
    render_screens(root, ["ui-lab"])

    xml = (root / "build/panorama/ui-lab/panorama/layout/custom_game/hud.xml").read_text(
        encoding="utf-8"
    )
    # src/Menu/PanoramaMenuLayout.cpp builds these same names from the screen name.
    for name in (
        '<Panel id="hud" class="screen hidden"',
        'id="hud_tab1"',
        'id="hud_tab1_icon"',
        "{s:tab1}",
        'id="hud_row1_button"',
        'id="hud_row1_decrease"',
        'id="hud_row1_increase"',
        "{s:row1_label}",
        "{s:row1_hint}",
        "{s:row1_value}",
        'id="hud_subtitle"',
        'id="hud_close"',
        'id="hud_empty"',
        'id="hud_prompt"',
        'id="hud_cancel"',
        'id="hud_back"',
        'id="hud_page"',
        'id="hud_page_previous"',
        'id="hud_page_next"',
        "{s:brand}",
    ):
        assert name in xml
