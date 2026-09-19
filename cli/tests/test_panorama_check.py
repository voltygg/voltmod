"""Cover `voltmod panorama check`, one small rendered screen per rule."""

import pytest

from voltmod.panorama.check import check_screens

HIDDEN_CSS = ".screen.hidden {\n  visibility: collapse;\n}\n"

SCREEN = """<root>
  <styles>
    <include src="file://{resources}/styles/custom_game/{{screen}}.css" />
  </styles>
  <Panel id="{{screen}}" class="screen hidden">
BODY
  </Panel>
</root>
"""

CLEAN_BODY = """<Label text="{s:title}" />
<Button id="{{screen}}_close" class="nav">
  <Label text="Close" />
</Button>"""

CLEAN_CSS = HIDDEN_CSS + ".nav {\n  width: 100%;\n}\n"

# Well under the per-screen limit alone; three of these overflow the client's table.
CROWDED_BODY = """{%- for index in range(350) %}
<Panel id="{{screen}}_p{{index}}" />
{%- endfor %}"""


def screen(body: str) -> str:
    return SCREEN.replace("BODY", body)


def messages(root, owners=("ui-lab",)) -> list[str]:
    return [result.message for result in check_screens(root, list(owners))]


RULES = [
    (screen("<script>bad()</script>"), "<script> is not an allowed element"),
    (screen('<Button class="nav" />'), "<Button> has no id"),
    (
        screen('<Button id="{{screen}}_outer"><Button id="{{screen}}_inner" /></Button>'),
        "nested inside another Button",
    ),
    (screen('<Panel id="{{screen}}_row" />\n<Panel id="{{screen}}_row" />'), "used more than once"),
    (screen("").replace('id="{{screen}}"', 'id="other"'), "does not match source name 'hud'"),
    (screen('<Panel id="other" />'), "does not start with"),
    (
        SCREEN.replace("{{screen}}.css", "wrong.css").replace("BODY", ""),
        "expected one style include",
    ),
    (
        screen('<Image src="s2r://panorama/images/custom_game/weapons/missing.vtex" />'),
        "has no weapons/missing.png",
    ),
    (
        screen(
            '<Panel id="{{screen}}_row0" />\n<Panel id="{{screen}}_row1" />\n'
            '<Panel id="{{screen}}_rows" />'
        ),
        "both spell Rows",
    ),
    (
        screen(
            '{%- for index in range(901) %}\n<Panel id="{{screen}}_p{{index}}" />\n{%- endfor %}'
        ),
        "per-screen limit",
    ),
]


@pytest.mark.parametrize(("xml", "expected"), RULES)
def test_each_rule_is_flagged(make_screen_project, xml, expected):
    root = make_screen_project(xml=xml, css=HIDDEN_CSS)
    assert any(expected in message for message in messages(root))


def test_a_clean_screen_has_no_findings(make_screen_project):
    root = make_screen_project(xml=screen(CLEAN_BODY), css=CLEAN_CSS)
    assert messages(root) == []


def test_a_game_icon_needs_no_png(make_screen_project):
    body = '<Image src="s2r://panorama/images/icons/ui/settings.vsvg" />'
    root = make_screen_project(xml=screen(body), css=HIDDEN_CSS)
    assert messages(root) == []


def test_a_dialog_variable_shared_by_two_labels_is_not_flagged(make_screen_project):
    body = (
        '<Panel id="{{screen}}_a"><Label text="{s:label}" /></Panel>\n'
        '<Panel id="{{screen}}_b"><Label text="{s:label}" /></Panel>'
    )
    root = make_screen_project(xml=screen(body), css=HIDDEN_CSS)
    assert messages(root) == []


def test_two_owners_rendering_the_same_resource_are_named(make_screen_project):
    root = make_screen_project(xml=screen(CLEAN_BODY), css=CLEAN_CSS)
    make_screen_project(xml=screen(CLEAN_BODY), css=CLEAN_CSS, plugin="ui-second")

    found = messages(root, ("ui-lab", "ui-second"))

    assert any(
        "layout/custom_game/hud.xml" in message and "ui-lab" in message and "ui-second" in message
        for message in found
    )
    assert any("styles/custom_game/hud.css" in message for message in found)


def test_screens_that_each_pass_can_still_overflow_the_client_table(make_screen_project):
    for name in ("hud", "menu", "panel"):
        root = make_screen_project(xml=screen(CROWDED_BODY), css=HIDDEN_CSS, name=name)

    found = messages(root)

    assert not any("per-screen limit" in message for message in found)
    assert any("across all screens" in message for message in found)
