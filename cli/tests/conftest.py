from pathlib import Path

import pytest

# A 1x1 transparent PNG, so an icon set has real files in it.
PNG = bytes.fromhex(
    "89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c4"
    "890000000d4944415478da63f8cf0c000203010049810180a4a98c2100000000"
    "49454e44ae426082"
)

HUD_XML = """{% import "icons.xml.j2" as icons %}
{% import "listrow.xml.j2" as list %}
<root>
  <Panel class="layer" hittest="false">
    <Panel id="{{screen}}" class="screen" hittest="false">
      {{ icons.icons("slot0_icon", "weapons") }}
      {{ list.listrow("slot0", hint=true) }}
    </Panel>
  </Panel>
</root>
"""

HUD_CSS = """{% import "icons.css.j2" as icons %}
.screen {
  color: #e8e6e0;
  background-color: rgba(255, 255, 255, 0.05);
}
{% include "listrow.css.j2" %}
{{ icons.show_rules("weapons") }}
"""


@pytest.fixture
def make_screen_project(tmp_path: Path):
    """Write one screen and a `weapons` icon set into plugins/<plugin>; returns the repo root."""

    def create(
        xml: str = HUD_XML,
        css: str = HUD_CSS,
        name: str = "hud",
        icons: tuple[str, ...] = ("ak47",),
        plugin: str = "ui-lab",
    ) -> Path:
        panorama = tmp_path / "plugins" / plugin / "panorama"
        screens = panorama / "screens"
        screens.mkdir(parents=True, exist_ok=True)
        (screens / f"{name}.xml.j2").write_text(xml, encoding="utf-8")
        (screens / f"{name}.css.j2").write_text(css, encoding="utf-8")

        weapons = panorama / "images/weapons"
        weapons.mkdir(parents=True, exist_ok=True)
        for icon in icons:
            (weapons / f"{icon}.png").write_bytes(PNG)
        return tmp_path

    return create
