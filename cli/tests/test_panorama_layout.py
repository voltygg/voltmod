"""Cover the C++ header a rendered screen gets; plugins spell its constants, so it is a contract.

`tests/Ui/Fixtures/Lab.hpp` is the same header compiled by the C++ suite; one case regenerates it.
"""

import os
from pathlib import Path

import pytest

from voltmod.panorama.layout import read_screen, selector_classes
from voltmod.panorama.render import ScreenRenderer, screen_header
from voltmod.panorama.sources import screen_owners

REPO_ROOT = Path(__file__).resolve().parents[2]

LAB_XML = """{# namespace: LabUi #}
{% import "button.xml.j2" as controls %}
{% import "icons.xml.j2" as icons %}
{% import "listrow.xml.j2" as list %}
<root>
  <Panel class="Layer" hittest="false">
    <Panel id="{{screen}}" class="Screen Hidden" hittest="false">
      {{ icons.icons("icon", "weapons") }}
      {%- for index in range(2) %}
      {{ list.listrow("row" ~ index, switch=true, hint=true, steppers=true, chevron=true) }}
      {%- endfor %}
      {{ controls.button("close", "{s:close}") }}
    </Panel>
  </Panel>
</root>
"""

LAB_CSS = """{% import "icons.css.j2" as icons %}
.Screen {
  width: 420px;
  flow-children: down;
}

.Screen.Hidden {
  visibility: collapse;
}

{% include "listrow.css.j2" %}
{% include "button.css.j2" %}

{{ icons.show_rules("weapons") }}
"""


def header_for(xml: str, css: str = "", template_source: str = "") -> str:
    return screen_header(read_screen(xml, css), template_source)


def test_the_checked_in_fixture_header_is_what_rendering_writes(make_screen_project):
    """Set VOLTMOD_REFRESH_FIXTURES=1 to rewrite the fixture after a deliberate change."""
    root = make_screen_project(xml=LAB_XML, css=LAB_CSS, name="lab", icons=("ak47", "awp"))
    lab = screen_owners(root, ["ui-lab"])[0]
    layout, stylesheet = ScreenRenderer(lab, screen_owners(root)).render("lab")

    header = header_for(layout, stylesheet, LAB_XML)
    fixture = REPO_ROOT / "tests/Ui/Fixtures/Lab.hpp"
    if os.environ.get("VOLTMOD_REFRESH_FIXTURES"):
        fixture.write_text(header, encoding="utf-8", newline="\n")

    assert header == fixture.read_text(encoding="utf-8")


def test_a_variable_named_twice_is_emitted_once():
    header = header_for(
        '<root><Panel id="s"><Label text="{s:title}" /><Label text="{s:title}" /></Panel></root>'
    )
    assert header.count('std::string_view TitleVar = "title";') == 1


def test_a_family_keeps_the_order_it_was_declared_in():
    header = header_for(
        '<root><Panel id="s" /></root>', ".Accent--zulu { a: 1; }\n.Accent--alpha { a: 1; }\n"
    )
    assert header.index('"Accent--zulu"') < header.index('"Accent--alpha"')


def test_a_decimal_in_a_declaration_is_not_read_as_a_family():
    header = header_for('<root><Panel id="s" /></root>', ".Bar { width: 33.3--4%; }\n")
    assert "Classes" not in header


def test_a_define_is_not_read_as_part_of_the_next_selector():
    stylesheet = "@define red-soft: rgba(225, 39, 60, 0.6);\n.Row { color: red-soft; }\n"
    assert selector_classes(stylesheet) == ["Row"]


def test_without_a_directive_the_namespace_comes_from_the_screen():
    header = header_for('<root><Panel id="voltmod_menu" /></root>')
    assert "namespace Screens::VoltmodMenu" in header


def test_a_bare_indexed_variable_becomes_var():
    header = header_for(
        '<root><Panel id="s"><Button id="s_tab0"><Label text="{s:tab0}" /></Button>'
        '<Button id="s_tab1"><Label text="{s:tab1}" /></Button></Panel></root>'
    )
    assert "struct Tab\n{\n    std::string_view Id;\n    std::string_view Var;\n};" in header
    assert 'Tab{"s_tab1", "tab1"}' in header


@pytest.mark.parametrize(
    ("body", "flat_constant"),
    [
        ('<Panel id="s_card0"><Panel id="s_card0_bar" /></Panel>', 'Card0Bar = "s_card0_bar"'),
        ('<Panel id="s_row0" /><Panel id="s_row2" />', 'Row2 = "s_row2"'),
        (
            '<Panel id="s_row0"><Panel id="s_row0_accent" /></Panel><Panel id="s_row1" />',
            'Row0Accent = "s_row0_accent"',
        ),
    ],
    ids=["single copy", "gap in the indices", "copies differ"],
)
def test_a_block_that_does_not_repeat_cleanly_stays_flat(body, flat_constant):
    header = header_for(f'<root><Panel id="s">{body}</Panel></root>')
    assert "struct " not in header
    assert f"std::string_view {flat_constant};" in header


def test_the_array_follows_index_order_not_document_order():
    header = header_for(
        '<root><Panel id="s"><Panel id="s_row1" /><Panel id="s_row0" /></Panel></root>'
    )
    assert header.index('Row{"s_row0"}') < header.index('Row{"s_row1"}')
