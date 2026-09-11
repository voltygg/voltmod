"""Test gamedata matching with stable synthetic bytes; false results can misbind engine calls."""

import pytest
from voltmod.builder import gamedata
from voltmod.builder.gamedata import document


class FakeModules:
    """In-memory stand-in for `image.Modules`."""

    def __init__(self, data: bytes, platform: str = "windows") -> None:
        self.platform = platform
        self._data = data

    def find(self, library: str, pattern: str) -> list[int]:
        return [m.start() for m in gamedata.pattern_regex(pattern).finditer(self._data)]

    def read(self, library: str, at: int, length: int) -> bytes:
        return self._data[at : at + length]


def entry(pattern: str) -> dict:
    return {"signatures": {"Setter": {"windows": {"pattern": pattern}}}}


def test_a_pattern_that_still_matches_once_is_left_alone():
    modules = FakeModules(b"\x90\x90\x48\x8b\x01\x02\xc3")
    findings = gamedata.run(entry("48 8B 01 02"), modules, {})

    assert [f.status for f in findings] == ["holds"]
    assert findings[0].new == "", "a holding entry has nothing to write"


def test_a_pattern_matching_twice_is_ambiguous_rather_than_repaired():
    modules = FakeModules(b"\x48\x8b\x00\x00\x48\x8b\x00\x00")
    findings = gamedata.run(entry("48 8B"), modules, {})

    assert findings[0].status == "ambiguous"
    assert "2 matches" in findings[0].detail


def test_a_displacement_that_moved_is_found_and_wildcarded():
    """A field grew, moving the displacement in an otherwise matching function."""
    # The live displacement is 0x4B8; the stale pattern encodes 0x4B0.
    modules = FakeModules(b"\x00\x3b\x99\xb8\x04\x00\x00\x7d\x5e\x00")
    findings = gamedata.run(entry("3B 99 B0 04 00 00 7D 5E"), modules, {})

    assert findings[0].status == "repaired"
    assert findings[0].new == "3B 99 ? ? ? ? 7D 5E"
    assert "1200 -> 1208" in findings[0].detail


def test_a_repair_names_the_schema_field_the_new_offset_belongs_to():
    modules = FakeModules(b"\x00\x3b\x99\xb8\x04\x00\x00\x7d\x5e\x00")
    schema = {"classes": {"CCSCustomHudLayout": {"fields": [{"name": "m_vec", "offset": 1208}]}}}
    findings = gamedata.run(entry("3B 99 B0 04 00 00 7D 5E"), modules, schema)

    assert "CCSCustomHudLayout::m_vec" in findings[0].detail


def test_two_viable_displacements_are_refused_rather_than_guessed_between():
    """Two candidate displacements each produce one match, so repair must refuse."""
    modules = FakeModules(b"\x01\x00\x00\x00\x99\x02\x00\x00\x00")
    findings = gamedata.run(entry("05 00 00 00 99 06 00 00 00"), modules, {})

    assert findings[0].status == "broken"
    assert findings[0].new == ""


def test_a_miss_no_displacement_explains_is_reported_not_repaired():
    modules = FakeModules(b"\x90" * 64)
    findings = gamedata.run(entry("48 8B 01 02 03 04"), modules, {})

    assert findings[0].status == "broken"
    assert "no single displacement" in findings[0].detail


def test_an_entry_for_the_other_platform_is_skipped():
    modules = FakeModules(b"\x90" * 16, platform="linux")
    findings = gamedata.run(entry("48 8B"), modules, {})

    assert findings == []


def test_patching_a_pattern_leaves_every_comment_and_blank_line_intact():
    text = (
        '{\n  // Why this entry exists.\n'
        '  "signatures": {\n    "Setter": { "windows": { "pattern": "3B 99 B0 04" } }\n  }\n}\n'
    )
    patched = document.set_pattern(text, "Setter", "3B 99 B0 04", "3B 99 ? ?")

    assert "// Why this entry exists." in patched
    assert '"pattern": "3B 99 ? ?"' in patched
    assert patched.count("\n") == text.count("\n"), "no line was added or removed"


def test_patching_an_entry_the_file_does_not_have_is_refused():
    with pytest.raises(SystemExit, match="no entry named Missing"):
        document.set_pattern('{"signatures": {}}', "Missing", "48", "??")

