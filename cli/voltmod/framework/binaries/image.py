import re
from dataclasses import dataclass
from functools import cached_property

_AFTER_PADDING = re.compile(rb"\xcc(?=[^\xcc])", re.DOTALL)
_FUNCTION_ALIGNMENT = 16


@dataclass(frozen=True, slots=True)
class Section:
    name: str
    address: int
    size: int
    offset: int  # in the file, which holds all `size` bytes
    executable: bool

    def holds(self, address: int, length: int = 1) -> bool:
        return self.address <= address and address + length <= self.address + self.size


def padding_ends(code: bytes, address: int) -> set[int]:
    """Aligned addresses right after int3 padding: where compilers start functions."""
    ends = (address + match.end() for match in _AFTER_PADDING.finditer(code))
    return {end for end in ends if end % _FUNCTION_ALIGNMENT == 0}


class Image:
    """A game module read from disk, addressed as if loaded at 0.

    Pointers in the file already hold their targets, offset by `image_base`.
    """

    image_base = 0

    def __init__(self, data: bytes, sections: list[Section]) -> None:
        self.data = data
        self.sections = sections

    def section(self, name: str) -> Section | None:
        return next((section for section in self.sections if section.name == name), None)

    def code_sections(self) -> list[Section]:
        return [section for section in self.sections if section.executable]

    def read(self, address: int, length: int) -> bytes | None:
        section = next((each for each in self.sections if each.holds(address, length)), None)
        if section is None:
            return None
        start = section.offset + address - section.address
        return self.data[start : start + length]

    def pointer(self, address: int) -> int | None:
        """Where the pointer at `address` points; None when it is null or unreadable."""
        raw = self.read(address, 8)
        value = int.from_bytes(raw, "little") if raw else 0
        return value - self.image_base if value else None

    def is_code(self, address: int | None) -> bool:
        return address is not None and any(each.holds(address) for each in self.code_sections())

    def slots(self, table: int) -> list[int]:
        """Each slot's target, up to the first slot that does not point at code."""
        targets: list[int] = []
        while self.is_code(target := self.pointer(table + 8 * len(targets))):
            targets.append(target)
        return targets

    def string_addresses(self, text: str) -> list[int]:
        needle = text.encode() + b"\0"
        data = (section for section in self.sections if not section.executable)
        return [address for section in data for address in self.find_in(section, needle)]

    def vtables(self, class_name: str, subobject: int = 0) -> list[int]:
        """Slot 0 of each table RTTI gives `class_name` at byte `subobject`; 0 is the primary."""
        raise NotImplementedError

    @cached_property
    def function_starts(self) -> list[int]:
        """Sorted function entries: unwind records plus the ends of int3 padding."""
        starts = self._unwind_starts()
        for section in self.code_sections():
            starts |= padding_ends(self._bytes_of(section), section.address)
        return sorted(starts)

    def find_in(self, section: Section, needle: bytes, alignment: int = 1) -> list[int]:
        found = []
        end = section.offset + section.size
        at = self.data.find(needle, section.offset, end)
        while at != -1:
            if (at - section.offset) % alignment == 0:
                found.append(section.address + at - section.offset)
            at = self.data.find(needle, at + 1, end)
        return found

    def _unwind_starts(self) -> set[int]:
        raise NotImplementedError

    def _bytes_of(self, section: Section) -> bytes:
        return self.data[section.offset : section.offset + section.size]
