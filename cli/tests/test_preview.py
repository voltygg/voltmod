"""Cover `voltmod panorama preview` against the shared card fixture and the framework's own menu.

Reuses `plugin()` from test_panorama.py, so a passing test has actually rendered and bound the
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
    assert "pvFamily('hud_slot0_icon', 'Icon'" in text
    assert "pvFamily('hud_slot0_bar', 'Step'" in text
    assert "<select" in text
    assert "data:image/png;base64," in text
    assert "display: flex" in text
    assert "{{" not in text


def test_the_framework_menu_previews_without_error(tmp_path):
    out = previewer.preview(tmp_path, KIT, "voltmod/voltmod_menu")
    text = out.read_text(encoding="utf-8")

    assert out == tmp_path / "build/panorama/preview/voltmod_menu.html"
    assert 'id="voltmod_menu_row9_btn"' in text
    assert "{{" not in text


def test_a_bad_target_format_dies_with_a_message(tmp_path):
    with pytest.raises(SystemExit) as error:
        previewer.preview(plugin(tmp_path), KIT, "hud")
    assert "OWNER/SCREEN" in str(error.value)


def test_an_unknown_owner_lists_the_known_ones(tmp_path):
    with pytest.raises(SystemExit) as error:
        previewer.preview(plugin(tmp_path), KIT, "nope/hud")
    assert "voltmod" in str(error.value) and "ui-lab" in str(error.value)


def test_an_unknown_screen_lists_the_owners_own(tmp_path):
    with pytest.raises(SystemExit) as error:
        previewer.preview(plugin(tmp_path), KIT, "ui-lab/nope")
    assert "hud" in str(error.value)
