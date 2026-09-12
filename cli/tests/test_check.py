"""Cover `voltmod panorama check` against small hand-built screens, one per rule.

Fixtures reuse `plugin()` from test_panorama.py so a fixture is a directory the renderer accepts,
not a mock of it - a check that passes here has actually rendered and bound the screen.
"""

from pathlib import Path

from test_panorama import plugin
from voltmod.builder.panorama import check as checker

FRAMEWORK = Path(__file__).resolve().parents[2]

#: A state class on the root, so a screen has something for the header to name.
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


def second_owner(root: Path, name: str = "hud") -> Path:
    """A second plugin whose screen can collide with `plugin()`'s by name."""
    panorama = root / "plugins/ui-second/panorama"
    (panorama / "screens").mkdir(parents=True, exist_ok=True)
    (panorama / "screens" / f"{name}.xml.j2").write_text(CLEAN_XML, encoding="utf-8")
    (panorama / "screens" / f"{name}.css").write_text(CLEAN_CSS, encoding="utf-8")
    return root


def test_disallowed_element_is_flagged(tmp_path):
    root = plugin(tmp_path, xml=SCRIPT_XML, css=HIDDEN_CSS, name="hud")
    findings = checker.check(root, FRAMEWORK, ["ui-lab"])
    assert any("<script>" in finding for finding in findings)


def test_button_without_id_is_flagged(tmp_path):
    root = plugin(tmp_path, xml=BUTTON_NO_ID_XML, css=HIDDEN_CSS, name="hud")
    findings = checker.check(root, FRAMEWORK, ["ui-lab"])
    assert any("<Button> has no id" in finding for finding in findings)


def test_button_nested_in_button_is_flagged(tmp_path):
    root = plugin(tmp_path, xml=NESTED_BUTTON_XML, css=HIDDEN_CSS, name="hud")
    findings = checker.check(root, FRAMEWORK, ["ui-lab"])
    assert any("nested inside another Button" in finding for finding in findings)


def test_duplicate_id_is_flagged(tmp_path):
    root = plugin(tmp_path, xml=DUPLICATE_ID_XML, css=HIDDEN_CSS, name="hud")
    findings = checker.check(root, FRAMEWORK, ["ui-lab"])
    assert any("used more than once" in finding for finding in findings)


def test_id_outside_the_screen_prefix_is_flagged(tmp_path):
    root = plugin(tmp_path, xml=STRAY_ID_XML, css=HIDDEN_CSS, name="hud")
    findings = checker.check(root, FRAMEWORK, ["ui-lab"])
    assert any("does not start with" in finding for finding in findings)


def test_missing_stylesheet_include_is_flagged(tmp_path):
    root = plugin(tmp_path, xml=WRONG_INCLUDE_XML, css=HIDDEN_CSS, name="hud")
    findings = checker.check(root, FRAMEWORK, ["ui-lab"])
    assert any("expected one style include" in finding for finding in findings)


def test_unknown_image_is_flagged(tmp_path):
    root = plugin(tmp_path, xml=UNKNOWN_IMAGE_XML, css=HIDDEN_CSS, name="hud")
    findings = checker.check(root, FRAMEWORK, ["ui-lab"])
    assert any("has no weapons/missing.png" in finding for finding in findings)


def test_two_owners_rendering_the_same_resource_are_named(tmp_path):
    root = plugin(tmp_path, xml=CLEAN_XML, css=CLEAN_CSS, name="hud")
    second_owner(root, name="hud")

    findings = checker.check(root, FRAMEWORK, ["ui-lab", "ui-second"])

    assert any(
        "layout/custom_game/hud.xml" in finding and "ui-lab" in finding and "ui-second" in finding
        for finding in findings
    )
    assert any("styles/custom_game/hud.css" in finding for finding in findings)


def test_a_shared_dialog_variable_is_not_flagged(tmp_path):
    root = plugin(tmp_path, xml=SHARED_VAR_XML, css=HIDDEN_CSS, name="hud")
    assert checker.check(root, FRAMEWORK, ["ui-lab"]) == []


def test_a_clean_screen_has_no_findings(tmp_path):
    root = plugin(tmp_path, xml=CLEAN_XML, css=CLEAN_CSS, name="hud")
    assert checker.check(root, FRAMEWORK, ["ui-lab"]) == []


ARRAY_CLASH_XML = """<root>
  <styles>
    <include src="file://{resources}/styles/custom_game/{{screen}}.css" />
  </styles>
  <Panel id="{{screen}}" class="Screen Hidden">
    <Panel id="{{screen}}_row0" />
    <Panel id="{{screen}}_row1" />
    <Panel id="{{screen}}_rows" />
  </Panel>
</root>
"""


def test_an_id_spelling_a_repeated_blocks_array_is_flagged(tmp_path):
    root = plugin(tmp_path, xml=ARRAY_CLASH_XML, css=HIDDEN_CSS, name="hud")
    findings = checker.check(root, FRAMEWORK, ["ui-lab"])
    assert any("both spell Rows" in finding for finding in findings)


RUNAWAY_XML = """<root>
  <styles>
    <include src="file://{resources}/styles/custom_game/{{screen}}.css" />
  </styles>
  <Panel id="{{screen}}" class="Screen Hidden">
    {%- for index in range(901) %}
    <Panel id="{{screen}}_p{{index}}" />
    {%- endfor %}
  </Panel>
</root>
"""

#: Well under the per-screen limit on its own; three of these overflow the client's table.
CROWDED_XML = """<root>
  <styles>
    <include src="file://{resources}/styles/custom_game/{{screen}}.css" />
  </styles>
  <Panel id="{{screen}}" class="Screen Hidden">
    {%- for index in range(350) %}
    <Panel id="{{screen}}_p{{index}}" />
    {%- endfor %}
  </Panel>
</root>
"""


def test_a_screen_over_the_per_screen_limit_is_flagged(tmp_path):
    root = plugin(tmp_path, xml=RUNAWAY_XML, css=HIDDEN_CSS, name="hud")
    findings = checker.check(root, FRAMEWORK, ["ui-lab"])
    assert any("per-screen limit" in finding for finding in findings)


def test_screens_that_each_pass_still_overflow_the_client_table(tmp_path):
    root = tmp_path
    for name in ("hud", "menu", "panel"):
        root = plugin(root, xml=CROWDED_XML, css=HIDDEN_CSS, name=name)

    findings = checker.check(root, FRAMEWORK, ["ui-lab"])

    assert not any("per-screen limit" in finding for finding in findings)
    assert any("across all screens" in finding for finding in findings)


def test_screens_within_the_client_table_are_not_flagged(tmp_path):
    root = plugin(tmp_path, xml=CROWDED_XML, css=HIDDEN_CSS, name="hud")
    findings = checker.check(root, FRAMEWORK, ["ui-lab"])
    assert not any("interned names" in finding for finding in findings)
