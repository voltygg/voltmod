"""Cover the derivation rules `voltmod panorama render` binds a screen by.

The rules are a contract: a screen's ids and classes decide what a plugin can write, so a change
here silently retypes every consumer's HUD. `tests/Ui/Fixtures/Lab.hpp` is the same derivation
compiled by the C++ suite; this regenerates it and compares.
"""

import os
from pathlib import Path

import pytest
from voltmod.builder.panorama import bind, render

KIT = Path(__file__).resolve().parents[2]

#: The screen the checked-in C++ fixture header is derived from.
LAB_XML = """{# namespace: LabUi #}
{% import "card.xml.j2" as cards %}
{% import "toast.xml.j2" as toasts %}
<root>
  <Panel class="Layer" hittest="false">
    <Panel id="{{screen}}" class="Screen Hidden" hittest="false">
      {%- for index in range(2) %}
      {{ cards.card("card" ~ index, icon_set="weapons", bar=true, accent=true) }}
      {%- endfor %}
      {{ toasts.toast("toast") }}
    </Panel>
  </Panel>
</root>
"""

LAB_CSS = """{% import "bar.css" as bar %}
{% import "icons.css" as icons %}
.Screen {
  width: 420px;
  flow-children: down;
}

.Screen.Hidden {
  visibility: collapse;
}

{% include "card.css" %}
{% include "toast.css" %}
{% include "accent.css" %}

.Accent.Accent--good {
  background-color: #4caf50;
}

.Accent.Accent--bad {
  background-color: #f44336;
}

{{ bar.fill_rules("Bar-fill", 4) }}
{{ icons.show_rules("weapons") }}
"""

#: A 1x1 transparent PNG, so an icon set has real files in it.
PNG = bytes.fromhex(
    "89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c4"
    "890000000d4944415478da63f8cf0c000203010049810180a4a98c2100000000"
    "49454e44ae426082"
)


def plugin(root: Path, xml: str = LAB_XML, css: str = LAB_CSS, name: str = "lab") -> Path:
    """A consumer repo with one plugin that owns one screen and one two-icon set."""
    panorama_dir = root / "plugins/ui-lab/panorama"
    (panorama_dir / "screens").mkdir(parents=True, exist_ok=True)
    (panorama_dir / "screens" / f"{name}.xml.j2").write_text(xml, encoding="utf-8")
    (panorama_dir / "screens" / f"{name}.css").write_text(css, encoding="utf-8")

    weapons = panorama_dir / "images/custom_game/weapons"
    weapons.mkdir(parents=True, exist_ok=True)
    for icon in ("ak47", "awp"):
        (weapons / f"{icon}.png").write_bytes(PNG)
    return root / "plugins/ui-lab"


def derive(xml: str, css: str, template: str = "") -> str:
    """The header for a layout and stylesheet written out in full, no Jinja in the way."""
    return bind.header(xml, css, template, "screen.xml.j2")


def fails(xml: str, css: str) -> str:
    with pytest.raises(SystemExit) as error:
        derive(xml, css)
    return str(error.value)


def test_digit_segments_nest_into_an_array_of_structs():
    header = derive(
        """<root>
             <Panel id="s">
               <Panel id="s_row0" class="Row"><Label text="{s:row0_label}" /></Panel>
               <Panel id="s_row1" class="Row"><Label text="{s:row1_label}" /></Panel>
             </Panel>
           </root>""",
        ".Row.Selected { opacity: 1; }",
    )

    assert "struct RowItem" in header
    assert "    VoltMod::Flag Selected;" in header
    assert "    VoltMod::Text Label;" in header
    assert "inline constexpr std::array<RowItem, 2> Row{{" in header
    assert '{"s_row1", "Selected"},' in header


def test_a_missing_index_is_named():
    message = fails(
        '<root><Panel id="s"><Panel id="s_row0" class="Row" /><Panel id="s_row2" class="Row" />'
        "</Panel></root>",
        ".Row.Selected { opacity: 1; }",
    )
    assert "Row1" in message


def test_a_family_comes_from_the_classes_the_stylesheet_pairs_up():
    header = derive(
        '<root><Panel id="s"><Panel id="s_toast_accent" class="Accent" /></Panel></root>',
        ".Accent.Accent--good { color: #0f0; }\n.Accent.Accent--bad { color: #f00; }",
    )

    assert "enum class Accent" in header
    assert "    Good,\n    Bad,\n};" in header
    assert 'inline constexpr std::array<std::string_view, 2> AccentNames{"good", "bad"};' in header
    assert 'AccentClasses{"Accent--good", "Accent--bad"};' in header
    assert "struct ToastPanel\n{\n    VoltMod::OneOf<2> Accent;\n};" in header
    assert 'ToastPanel Toast{\n    {"s_toast_accent", AccentClasses},\n};' in header


def test_a_member_may_not_take_a_family_name():
    message = fails(
        '<root><Panel id="s"><Panel id="s_accent" class="Accent" /></Panel></root>',
        ".Accent.Accent--good { color: #0f0; }",
    )
    assert "Accent names both" in message


def test_a_family_comes_from_the_children_of_an_icon_set():
    header = derive(
        """<root><Panel id="s"><Panel id="s_card_icon" class="Icons">
             <Image class="Icon--ak47" /><Image class="Icon--awp" />
           </Panel></Panel></root>""",
        ".Icons Image { visibility: collapse; }",
    )

    assert "enum class Icon" in header
    assert 'inline constexpr CardPanel Card{\n    {"s_card_icon", IconClasses},\n};' in header


def test_only_images_make_a_child_family():
    header = derive(
        """<root><Panel id="s">
             <Panel class="Region Region--top" /><Panel class="Region Region--bottom" />
             <Panel id="s_prompt" class="Prompt">
               <Label text="{s:prompt_text}" /><Button class="Nav Nav--narrow" />
             </Panel>
           </Panel></root>""",
        ".Prompt { width: 100%; }",
    )

    assert "Region" not in header and "Nav" not in header


def test_a_step_family_gets_classes_but_no_enum():
    header = derive(
        '<root><Panel id="s"><Panel id="s_bar" class="Bar-fill" /></Panel></root>',
        ".Bar-fill.Step--0 { width: 0%; }\n.Bar-fill.Step--1 { width: 100%; }",
    )

    assert "enum class Step" not in header and "StepNames" not in header
    assert 'StepClasses{"Step--0", "Step--1"};' in header
    assert 'inline constexpr VoltMod::OneOf<2> Bar{"s_bar", StepClasses};' in header


def test_a_panel_with_one_writer_takes_its_own_name():
    header = derive(
        '<root><Panel id="s"><Panel id="s_card0_accent" class="Accent" /></Panel></root>',
        ".Accent.Accent--good { color: #0f0; }",
    )

    assert "struct CardItem\n{\n    VoltMod::OneOf<1> Accent;\n};" in header
    assert 'std::array<CardItem, 1> Card{{\n    {\n        {"s_card0_accent"' in header


def test_a_panel_with_several_writers_becomes_a_struct():
    header = derive(
        """<root><Panel id="s"><Panel id="s_toast" class="Toast">
             <Label text="{s:toast_title}" />
           </Panel></Panel></root>""",
        ".Toast.Show { opacity: 1; }",
    )

    assert "struct ToastPanel\n{\n    VoltMod::Flag Show;\n    VoltMod::Text Title;\n};" in header
    assert "inline constexpr ToastPanel Toast{" in header


def test_hidden_on_the_root_is_a_flag_of_its_own():
    header = derive(
        '<root><Panel id="s" class="Screen Hidden"><Label text="{s:title}" /></Panel></root>',
        ".Screen.Hidden { visibility: collapse; }\n.Screen.Prompting { opacity: 0.5; }",
    )

    assert 'inline constexpr std::string_view Layout = "s";' in header
    assert 'inline constexpr std::string_view RootId = "s";' in header
    assert 'inline constexpr VoltMod::Flag Hidden{RootId, "Hidden"};' in header
    assert 'inline constexpr VoltMod::Flag Prompting{RootId, "Prompting"};' in header


def test_a_shared_vocabulary_class_says_nothing_about_another_panel():
    header = derive(
        '<root><Panel id="s"><Panel id="s_row" class="Row Hidden" /></Panel></root>',
        ".Nav-tab.Hidden { visibility: collapse; }\n.Row.Selected { opacity: 1; }",
    )

    assert "Nav" not in header
    assert 'inline constexpr VoltMod::Flag Row{"s_row", "Selected"};' in header


def test_the_namespace_comes_from_the_template_front_matter():
    layout = '<root><Panel id="s" class="Screen Hidden" /></root>'
    css = ".Screen.Hidden { visibility: collapse; }"

    assert "namespace Screens::S\n" in derive(layout, css)
    assert "namespace Cs2Ui::Hud\n" in derive(layout, css, "{# namespace: Cs2Ui::Hud #}\n<root>")


def test_two_writers_on_one_member_are_named():
    message = fails(
        """<root><Panel id="s"><Panel id="s_card0" class="Card">
             <Label text="{s:card0_hidden}" />
           </Panel></Panel></root>""",
        ".Card.Hidden { visibility: collapse; }",
    )
    assert "Hidden" in message


def test_an_id_outside_the_screen_is_named():
    message = fails('<root><Panel id="s"><Panel id="other" class="Row" /></Panel></root>', "")
    assert "'other'" in message and "'s_'" in message


def test_a_screen_with_nothing_to_write_is_refused():
    assert "nothing to bind" in fails('<root><Panel id="s" class="Screen" /></root>', "")


def test_the_framework_menu_binds(tmp_path):
    owner = render.Owner("voltmod", KIT / "panorama")
    layout, stylesheet = render.screen(owner, tmp_path, KIT, "voltmod_menu")

    text = bind.header(layout, stylesheet, "", "voltmod_menu.xml.j2")

    assert "namespace Screens::VoltmodMenu" in text
    assert "inline constexpr std::array<RowItem, 10> Row{{" in text
    assert "inline constexpr std::array<NavItem, 8> Nav{{" in text
    assert 'inline constexpr VoltMod::Flag Hidden{RootId, "Hidden"};' in text
    assert max(len(line) for line in text.splitlines()) <= bind.COLUMNS


def test_rendering_a_plugin_screen_writes_its_binding_header(tmp_path):
    plugin(tmp_path)
    header_path = tmp_path / "build/panorama/ui-lab/include/Ui/Lab.hpp"

    written = render.render(tmp_path, KIT, ["ui-lab"])

    assert header_path in written
    assert "namespace LabUi" in header_path.read_text(encoding="utf-8")
    assert render.render(tmp_path, KIT, ["ui-lab"]) == []  # write-if-changed, like the layouts


def test_the_framework_owner_gets_no_binding_header(tmp_path):
    written = render.render(tmp_path, KIT, ["voltmod"])

    assert written and not (tmp_path / "build/panorama/voltmod/include").exists()
    assert not [path for path in written if path.suffix == ".hpp"]


def test_the_checked_in_fixture_header_is_what_the_binder_writes(tmp_path):
    """Set VOLTMOD_REFRESH_FIXTURES=1 to rewrite the fixture after a deliberate rule change."""
    directory = plugin(tmp_path)
    owner = render.Owner("ui-lab", directory / "panorama")
    layout, stylesheet = render.screen(owner, tmp_path, KIT, "lab")
    template = (directory / "panorama/screens/lab.xml.j2").read_text(encoding="utf-8")

    header = bind.header(layout, stylesheet, template, "lab.xml.j2")
    fixture = KIT / "tests/Ui/Fixtures/Lab.hpp"
    if os.environ.get("VOLTMOD_REFRESH_FIXTURES"):
        fixture.parent.mkdir(parents=True, exist_ok=True)
        fixture.write_text(header, encoding="utf-8")

    assert header == fixture.read_text(encoding="utf-8")
