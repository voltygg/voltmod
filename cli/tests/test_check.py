"""Cover `voltmod panorama check` against small hand-built screens, one per rule.

Fixtures reuse `plugin()` from test_panorama.py so a fixture is a directory the renderer accepts,
not a mock of it - a check that passes here has actually rendered and bound the screen.
"""

from pathlib import Path

from test_panorama import plugin
from voltmod.builder.panorama import check as checker

KIT = Path(__file__).resolve().parents[2]

#: Enough for `bind.header` to find something to bind: a Hidden flag on the root.
HIDDEN_CSS = ".Screen.Hidden {\n  visibility: collapse;\n}\n"

SCRIPT_XML = """<root>
  <styles>
    <include src="file://{resources}/styles/custom_game/{{screen}}.css" />
  </styles>
  <Panel id="{{screen}}" class="Screen Hidden">
    <script>bad()</script>
  </Panel>
</root>
"""

BUTTON_NO_ID_XML = """<root>
  <styles>
    <include src="file://{resources}/styles/custom_game/{{screen}}.css" />
  </styles>
  <Panel id="{{screen}}" class="Screen Hidden">
    <Button class="Nav" />
  </Panel>
</root>
"""

NESTED_BUTTON_XML = """<root>
  <styles>
    <include src="file://{resources}/styles/custom_game/{{screen}}.css" />
  </styles>
  <Panel id="{{screen}}" class="Screen Hidden">
    <Button id="{{screen}}_outer">
      <Button id="{{screen}}_inner" />
    </Button>
  </Panel>
</root>
"""

DUPLICATE_ID_XML = """<root>
  <styles>
    <include src="file://{resources}/styles/custom_game/{{screen}}.css" />
  </styles>
  <Panel id="{{screen}}" class="Screen Hidden">
    <Panel id="{{screen}}_row" />
    <Panel id="{{screen}}_row" />
  </Panel>
</root>
"""

STRAY_ID_XML = """<root>
  <styles>
    <include src="file://{resources}/styles/custom_game/{{screen}}.css" />
  </styles>
  <Panel id="{{screen}}" class="Screen Hidden">
    <Panel id="other" />
  </Panel>
</root>
"""

WRONG_INCLUDE_XML = """<root>
  <styles>
    <include src="file://{resources}/styles/custom_game/wrong.css" />
  </styles>
  <Panel id="{{screen}}" class="Screen Hidden" />
</root>
"""

UNKNOWN_IMAGE_XML = """<root>
  <styles>
    <include src="file://{resources}/styles/custom_game/{{screen}}.css" />
  </styles>
  <Panel id="{{screen}}" class="Screen Hidden">
    <Image src="s2r://panorama/images/custom_game/weapons/missing.vtex" />
  </Panel>
</root>
"""

NOTHING_TO_BIND_XML = """<root>
  <styles>
    <include src="file://{resources}/styles/custom_game/{{screen}}.css" />
  </styles>
  <Panel id="{{screen}}" class="Screen" />
</root>
"""

#: The same {s:label} on two Labels: one writer, so this must not be flagged as a collision.
SHARED_VAR_XML = """<root>
  <styles>
    <include src="file://{resources}/styles/custom_game/{{screen}}.css" />
  </styles>
  <Panel id="{{screen}}" class="Screen Hidden">
    <Panel id="{{screen}}_a"><Label text="{s:label}" /></Panel>
    <Panel id="{{screen}}_b"><Label text="{s:label}" /></Panel>
  </Panel>
</root>
"""

CLEAN_XML = """<root>
  <styles>
    <include src="file://{resources}/styles/custom_game/{{screen}}.css" />
  </styles>
  <Panel id="{{screen}}" class="Screen Hidden">
    <Label text="{s:title}" />
    <Button id="{{screen}}_close" class="Nav">
      <Label text="Close" />
    </Button>
  </Panel>
</root>
"""

CLEAN_CSS = """.Screen.Hidden {
  visibility: collapse;
}
.Nav {
  width: 100%;
}
"""


def budget_xml(count: int = 920) -> str:
    """A screen whose ids alone cross the interned-name budget."""
    rows = "\n".join(
        '    <Panel id="{{screen}}_row' + str(index) + '" class="Row" />' for index in range(count)
    )
    return (
        "<root>\n"
        "  <styles>\n"
        '    <include src="file://{resources}/styles/custom_game/{{screen}}.css" />\n'
        "  </styles>\n"
        '  <Panel id="{{screen}}" class="Screen Hidden">\n'
        + rows
        + "\n  </Panel>\n</root>\n"
    )


def second_owner(root: Path, name: str = "hud") -> Path:
    """A second plugin whose screen can collide with `plugin()`'s by name."""
    panorama = root / "plugins/ui-second/panorama"
    (panorama / "screens").mkdir(parents=True, exist_ok=True)
    (panorama / "screens" / f"{name}.xml.j2").write_text(CLEAN_XML, encoding="utf-8")
    (panorama / "screens" / f"{name}.css").write_text(CLEAN_CSS, encoding="utf-8")
    return root


def test_disallowed_element_is_flagged(tmp_path):
    root = plugin(tmp_path, xml=SCRIPT_XML, css=HIDDEN_CSS, name="hud")
    findings = checker.check(root, KIT, ["ui-lab"])
    assert any("<script>" in finding for finding in findings)


def test_button_without_id_is_flagged(tmp_path):
    root = plugin(tmp_path, xml=BUTTON_NO_ID_XML, css=HIDDEN_CSS, name="hud")
    findings = checker.check(root, KIT, ["ui-lab"])
    assert any("<Button> has no id" in finding for finding in findings)


def test_button_nested_in_button_is_flagged(tmp_path):
    root = plugin(tmp_path, xml=NESTED_BUTTON_XML, css=HIDDEN_CSS, name="hud")
    findings = checker.check(root, KIT, ["ui-lab"])
    assert any("nested inside another Button" in finding for finding in findings)


def test_duplicate_id_is_flagged(tmp_path):
    root = plugin(tmp_path, xml=DUPLICATE_ID_XML, css=HIDDEN_CSS, name="hud")
    findings = checker.check(root, KIT, ["ui-lab"])
    assert any("used more than once" in finding for finding in findings)


def test_id_outside_the_screen_prefix_is_flagged(tmp_path):
    root = plugin(tmp_path, xml=STRAY_ID_XML, css=HIDDEN_CSS, name="hud")
    findings = checker.check(root, KIT, ["ui-lab"])
    assert any("does not start with" in finding for finding in findings)


def test_missing_stylesheet_include_is_flagged(tmp_path):
    root = plugin(tmp_path, xml=WRONG_INCLUDE_XML, css=HIDDEN_CSS, name="hud")
    findings = checker.check(root, KIT, ["ui-lab"])
    assert any("expected one style include" in finding for finding in findings)


def test_unknown_image_is_flagged(tmp_path):
    root = plugin(tmp_path, xml=UNKNOWN_IMAGE_XML, css=HIDDEN_CSS, name="hud")
    findings = checker.check(root, KIT, ["ui-lab"])
    assert any("has no weapons/missing.png" in finding for finding in findings)


def test_name_budget_over_900_is_flagged(tmp_path):
    root = plugin(tmp_path, xml=budget_xml(), css=HIDDEN_CSS, name="hud")
    findings = checker.check(root, KIT, ["ui-lab"])
    assert any("exceeds the 900 budget" in finding for finding in findings)


def test_binding_failure_is_flagged(tmp_path):
    root = plugin(tmp_path, xml=NOTHING_TO_BIND_XML, css="", name="hud")
    findings = checker.check(root, KIT, ["ui-lab"])
    assert any("nothing to bind" in finding for finding in findings)


def test_two_owners_rendering_the_same_resource_are_named(tmp_path):
    root = plugin(tmp_path, xml=CLEAN_XML, css=CLEAN_CSS, name="hud")
    second_owner(root, name="hud")

    findings = checker.check(root, KIT, ["ui-lab", "ui-second"])

    assert any(
        "layout/custom_game/hud.xml" in finding
        and "ui-lab" in finding
        and "ui-second" in finding
        for finding in findings
    )
    assert any("styles/custom_game/hud.css" in finding for finding in findings)


def test_a_shared_dialog_variable_is_not_flagged(tmp_path):
    root = plugin(tmp_path, xml=SHARED_VAR_XML, css=HIDDEN_CSS, name="hud")
    assert checker.check(root, KIT, ["ui-lab"]) == []


def test_a_clean_screen_has_no_findings(tmp_path):
    root = plugin(tmp_path, xml=CLEAN_XML, css=CLEAN_CSS, name="hud")
    assert checker.check(root, KIT, ["ui-lab"]) == []


def test_the_framework_menu_passes(tmp_path):
    assert checker.check(tmp_path, KIT, ["voltmod"]) == []
