import pytest

from voltmod.errors import VoltmodError
from voltmod.framework.gamedata import check_patterns, pattern_regex, replace_pattern


class FakeBinaries:
    """In-memory stand-in for `GameBinaries`."""

    def __init__(self, contents: bytes, platform: str = "windows") -> None:
        self.platform = platform
        self._contents = contents

    def find(self, module: str, pattern: str) -> list[int]:
        return [match.start() for match in pattern_regex(pattern).finditer(self._contents)]

    def read(self, module: str, offset: int, length: int) -> bytes:
        return self._contents[offset : offset + length]


def gamedata(pattern: str) -> dict:
    return {"functions": {"Setter": {"windows": pattern}}}


@pytest.mark.parametrize(
    ("section", "column"),
    [("functions", "48 8B 05"), ("globals", {"pattern": "48 8B 05", "rel32At": 3})],
    ids=["function pattern", "global column"],
)
def test_a_pattern_that_still_matches_once_is_left_alone(section, column):
    binaries = FakeBinaries(b"\x90\x48\x8b\x05\x10\x00\x00\x00")
    results = check_patterns({section: {"Entry": {"windows": column}}}, binaries, {})

    assert [(result.section, result.status) for result in results] == [(section, "unique")]
    assert results[0].new_pattern == "", "a unique entry has nothing to write"


def test_a_pattern_matching_twice_is_ambiguous_rather_than_repaired():
    binaries = FakeBinaries(b"\x48\x8b\x00\x00\x48\x8b\x00\x00")
    results = check_patterns(gamedata("48 8B"), binaries, {})

    assert results[0].status == "ambiguous"
    assert "2 matches" in results[0].detail


def test_a_moved_offset_is_wildcarded_and_named_after_its_schema_field():
    """A field grew, moving the offset in an otherwise matching function."""
    # The live offset is 0x4B8; the stale pattern encodes 0x4B0.
    binaries = FakeBinaries(b"\x00\x3b\x99\xb8\x04\x00\x00\x7d\x5e\x00")
    schema = {"classes": {"CCSCustomHudLayout": {"fields": [{"name": "m_vec", "offset": 1208}]}}}
    results = check_patterns(gamedata("3B 99 B0 04 00 00 7D 5E"), binaries, schema)

    assert results[0].status == "repaired"
    assert results[0].new_pattern == "3B 99 ? ? ? ? 7D 5E"
    assert "1200 -> 1208" in results[0].detail
    assert "CCSCustomHudLayout::m_vec" in results[0].detail


@pytest.mark.parametrize(
    ("contents", "pattern"),
    [
        (b"\x01\x00\x00\x00\x99\x02\x00\x00\x00", "05 00 00 00 99 06 00 00 00"),
        (b"\x90" * 64, "48 8B 01 02 03 04"),
    ],
    ids=["two viable offsets", "no offset explains it"],
)
def test_a_miss_is_repaired_only_when_exactly_one_offset_explains_it(contents, pattern):
    results = check_patterns(gamedata(pattern), FakeBinaries(contents), {})

    assert results[0].status == "missing"
    assert results[0].new_pattern == ""
    assert "no single moved offset" in results[0].detail


def test_an_entry_for_the_other_platform_is_skipped():
    binaries = FakeBinaries(b"\x90" * 16, platform="linux")
    assert check_patterns(gamedata("48 8B"), binaries, {}) == []


def test_replacing_a_pattern_keeps_every_comment_and_blank_line():
    text = (
        "{\n  // Why this entry exists.\n"
        '  "functions": {\n    "Setter": { "windows": "3B 99 B0 04" }\n  }\n}\n'
    )
    patched = replace_pattern(text, "Setter", "3B 99 B0 04", "3B 99 ? ?")

    assert "// Why this entry exists." in patched
    assert '"windows": "3B 99 ? ?"' in patched
    assert patched.count("\n") == text.count("\n"), "no line was added or removed"


def test_replacing_an_entry_the_file_does_not_have_is_refused():
    with pytest.raises(VoltmodError, match="no entry named Missing"):
        replace_pattern('{"functions": {}}', "Missing", "48", "??")
