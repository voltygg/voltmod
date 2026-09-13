"""Cover `voltmod panorama preview` against the shared card screen."""

import pytest

from voltmod.errors import VoltmodError
from voltmod.panorama.preview import write_preview


def test_the_card_screen_previews_its_variables_flags_families_and_image(make_screen_project):
    root = make_screen_project()
    out = write_preview(root, "ui-lab/hud")
    text = out.read_text(encoding="utf-8")

    assert out == root / "build/panorama/preview/hud.html"
    assert '<span data-var="slot0_title">slot0_title</span>' in text
    assert 'data-pv-var="slot0_title"' in text
    assert '<span data-var="slot0_subtitle">' in text
    assert '<span data-var="slot0_value">' in text
    assert 'data-pv-flag="Hidden" data-pv-id="hud_slot0"' in text
    assert 'type="checkbox"' in text
    # A family is offered with a panel picker: which panel carries it is the plugin's business.
    assert 'data-pv-family="Icon" data-pv-role="panel"' in text
    assert 'data-pv-family="Step" data-pv-role="variant"' in text
    assert 'value="hud_slot0_icon"' in text
    assert "data:image/png;base64," in text
    assert "display: flex" in text
    # The screen's own rules, which reach the page only through the template's css slot.
    assert "flex-shrink: 0" in text
    assert "{{" not in text and "{ {" not in text


@pytest.mark.parametrize(
    ("target", "expected"),
    [("hud", ["OWNER/SCREEN"]), ("nope/hud", ["nope", "ui-lab"]), ("ui-lab/nope", ["hud"])],
    ids=["not owner/screen", "unknown owner", "unknown screen"],
)
def test_a_bad_target_says_what_is_known(make_screen_project, target, expected):
    with pytest.raises(VoltmodError) as error:
        write_preview(make_screen_project(), target)
    assert all(word in str(error.value) for word in expected)
