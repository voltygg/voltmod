"""Cover `voltmod panorama preview` against the shared card fixture.

Reuses `plugin()` from test_panorama.py, so a passing test has actually rendered and read the
fixture screen, not a mock of it.
"""

from pathlib import Path

import pytest
from test_panorama import plugin
from voltmod.builder.panorama import preview as previewer

KIT = Path(__file__).resolve().parents[2]


def test_the_card_fixture_previews_its_variables_flags_families_and_image(tmp_path):
    root = plugin(tmp_path)
    out = previewer.preview(root, KIT, "ui-lab/hud")
    text = out.read_text(encoding="utf-8")

    assert out == root / "build/panorama/preview/hud.html"
    assert '<span data-var="slot0_title">slot0_title</span>' in text
    assert '<span data-var="slot0_subtitle">' in text
    assert '<span data-var="slot0_value">' in text
    assert "pvFlag('hud_slot0', 'Hidden'" in text
    assert 'type="checkbox"' in text
    # A family is offered with a panel picker: which panel carries it is the plugin's business.
    assert "pvPick('Icon')" in text and 'id="pv-Icon-panel"' in text
    assert 'value="hud_slot0_icon"' in text
    assert "pvPick('Step')" in text and 'id="pv-Step-variant"' in text
    assert "data:image/png;base64," in text
    assert "display: flex" in text
    assert "{{" not in text


def test_a_bad_target_format_dies_with_a_message(tmp_path):
    with pytest.raises(SystemExit) as error:
        previewer.preview(plugin(tmp_path), KIT, "hud")
    assert "OWNER/SCREEN" in str(error.value)


def test_an_unknown_owner_lists_the_known_ones(tmp_path):
    with pytest.raises(SystemExit) as error:
        previewer.preview(plugin(tmp_path), KIT, "nope/hud")
    assert "nope" in str(error.value) and "ui-lab" in str(error.value)


def test_an_unknown_screen_lists_the_owners_own(tmp_path):
    with pytest.raises(SystemExit) as error:
        previewer.preview(plugin(tmp_path), KIT, "ui-lab/nope")
    assert "hud" in str(error.value)
