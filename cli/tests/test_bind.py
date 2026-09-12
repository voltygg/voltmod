"""Cover what `voltmod panorama render` writes into a screen's C++ header.

The header is a contract: a plugin spells its panels, dialog variables and class families
through these constants, so a change here retypes every consumer. `tests/Ui/Fixtures/Lab.hpp` is
the same output compiled by the C++ suite; the last case regenerates it and compares.

Nothing here checks a screen. `bind` only emits; `test_check.py` covers the rules.
"""

import os
from pathlib import Path

from voltmod.builder.panorama import bind, screens

FRAMEWORK = Path(__file__).resolve().parents[2]

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

{{ bar.fill_rules("Bar", 4) }}
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


def derive(xml: str, css: str = "", template: str = "") -> str:
    """The header for a layout and stylesheet written out in full, no Jinja in the way."""
    return bind.header(bind.read(xml, css), template)


def test_panel_ids_are_named_by_what_follows_the_screen():
    header = derive('<root><Panel id="s"><Panel id="s_card0_bar" /></Panel></root>')

    assert 'inline constexpr std::string_view Layout = "s";' in header
    assert 'inline constexpr std::string_view RootId = "s";' in header
    assert 'inline constexpr std::string_view Card0Bar = "s_card0_bar";' in header


def test_dialog_variables_take_a_var_suffix():
    header = derive('<root><Panel id="s"><Label text="{s:card0_title}" /></Panel></root>')

    assert 'inline constexpr std::string_view Card0TitleVar = "card0_title";' in header


def test_a_variable_named_twice_is_emitted_once():
    header = derive(
        '<root><Panel id="s"><Label text="{s:title}" /><Label text="{s:title}" /></Panel></root>'
    )

    assert header.count('std::string_view TitleVar = "title";') == 1


def test_a_family_declared_only_in_the_stylesheet_is_found():
    header = derive(
        '<root><Panel id="s"><Panel id="s_accent" class="Accent" /></Panel></root>',
        ".Accent.Accent--good { color: #0f0; }\n.Accent.Accent--bad { color: #f00; }\n",
    )

    assert "enum class Accent" in header
    assert 'inline constexpr std::array<std::string_view, 2> AccentNames{"good", "bad"};' in header
    assert '"Accent--good", "Accent--bad"' in header


def test_a_family_keeps_the_order_it_was_declared_in():
    header = derive(
        '<root><Panel id="s" /></root>',
        ".Accent--zulu { a: 1; }\n.Accent--alpha { a: 1; }\n",
    )

    assert header.index('"Accent--zulu"') < header.index('"Accent--alpha"')


def test_a_decimal_in_a_declaration_is_not_read_as_a_family():
    header = derive('<root><Panel id="s" /></root>', ".Bar { width: 33.3--4%; }\n")

    assert "enum class" not in header


def test_a_digit_only_family_gets_classes_but_no_enum():
    header = derive(
        '<root><Panel id="s" /></root>',
        ".Bar-fill.Step--0 { width: 0%; }\n.Bar-fill.Step--1 { width: 100%; }\n",
    )

    assert "enum class Step" not in header and "StepNames" not in header
    assert 'std::array<std::string_view, 2> StepClasses{"Step--0", "Step--1"};' in header


def test_the_template_names_the_namespace():
    header = derive('<root><Panel id="s" /></root>', "", "{# namespace: Some::Place #}")
    assert "namespace Some::Place" in header


def test_without_a_directive_the_namespace_is_derived_from_the_screen():
    assert "namespace Screens::VoltmodMenu" in derive('<root><Panel id="voltmod_menu" /></root>')


def test_a_long_array_is_broken_up_one_item_per_line():
    css = "\n".join(f".Accent--variant{index} {{ a: 1; }}" for index in range(12))
    header = derive('<root><Panel id="s" /></root>', css)

    assert max(len(line) for line in header.splitlines()) <= bind.COLUMNS


def test_rendering_a_plugin_screen_writes_its_header(tmp_path):
    plugin(tmp_path)
    header_path = tmp_path / "build/panorama/ui-lab/include/Ui/Lab.hpp"

    written = screens.render(tmp_path, FRAMEWORK, ["ui-lab"])

    assert header_path in written
    assert "namespace LabUi" in header_path.read_text(encoding="utf-8")
    # write-if-changed, like the layouts
    assert screens.render(tmp_path, FRAMEWORK, ["ui-lab"]) == []


def test_the_checked_in_fixture_header_is_what_the_binder_writes(tmp_path):
    """Set VOLTMOD_REFRESH_FIXTURES=1 to rewrite the fixture after a deliberate change."""
    directory = plugin(tmp_path)
    owner = screens.Owner("ui-lab", directory / "panorama")
    layout, stylesheet = screens.screen(owner, FRAMEWORK, "lab")
    template = (directory / "panorama/screens/lab.xml.j2").read_text(encoding="utf-8")

    header = bind.header(bind.read(layout, stylesheet), template)
    fixture = FRAMEWORK / "tests/Ui/Fixtures/Lab.hpp"
    if os.environ.get("VOLTMOD_REFRESH_FIXTURES"):
        fixture.parent.mkdir(parents=True, exist_ok=True)
        fixture.write_text(header, encoding="utf-8")

    assert header == fixture.read_text(encoding="utf-8")


ROW_XML = (
    '<Panel id="s_row{i}"><Panel id="s_row{i}_accent" />'
    '<Button id="s_row{i}_btn"><Label text="{{s:row{i}_label}}" /></Button></Panel>'
)
ROWS_XML = '<root><Panel id="s">' + ROW_XML.format(i=0) + ROW_XML.format(i=1) + "</Panel></root>"


def test_a_repeated_block_becomes_a_struct_and_an_array():
    header = derive(ROWS_XML)

    assert "struct Row\n{\n    std::string_view Id;\n    std::string_view Accent;\n" in header
    assert "    std::string_view Btn;\n    std::string_view LabelVar;\n};" in header
    assert "inline constexpr std::array<Row, 2> Rows{" in header
    assert '    Row{"s_row0", "s_row0_accent", "s_row0_btn", "row0_label"},' in header
    assert "Row0Accent" not in header and "Row0LabelVar" not in header


def test_a_bare_indexed_variable_becomes_var():
    header = derive(
        '<root><Panel id="s"><Button id="s_tab0"><Label text="{s:tab0}" /></Button>'
        '<Button id="s_tab1"><Label text="{s:tab1}" /></Button></Panel></root>'
    )

    assert "struct Tab\n{\n    std::string_view Id;\n    std::string_view Var;\n};" in header
    assert 'Tab{"s_tab1", "tab1"}' in header


def test_a_single_copy_stays_flat():
    header = derive(
        '<root><Panel id="s"><Panel id="s_card0"><Panel id="s_card0_bar" /></Panel></Panel></root>'
    )

    assert "struct Card" not in header
    assert 'std::string_view Card0Bar = "s_card0_bar";' in header


def test_a_gap_in_the_indices_stays_flat():
    header = derive('<root><Panel id="s"><Panel id="s_row0" /><Panel id="s_row2" /></Panel></root>')

    assert "struct Row" not in header
    assert 'std::string_view Row2 = "s_row2";' in header


def test_copies_that_differ_stay_flat():
    header = derive(
        '<root><Panel id="s"><Panel id="s_row0"><Panel id="s_row0_accent" /></Panel>'
        '<Panel id="s_row1" /></Panel></root>'
    )

    assert "struct Row" not in header
    assert 'std::string_view Row0Accent = "s_row0_accent";' in header


def test_the_array_follows_index_order_not_document_order():
    header = derive('<root><Panel id="s"><Panel id="s_row1" /><Panel id="s_row0" /></Panel></root>')

    assert header.index('Row{"s_row0"}') < header.index('Row{"s_row1"}')
