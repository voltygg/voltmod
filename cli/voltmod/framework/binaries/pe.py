import struct

from voltmod.errors import VoltmodError
from voltmod.framework.binaries.image import Image, Section

# MSVC x64 RTTI offsets, as in VtableLookup_Windows.cpp.
_DESCRIPTOR_NAME = 0x10
_LOCATOR_DESCRIPTOR = 0x0C
_LOCATOR_SELF = 0x14

_UNWIND_CHAINED = 0x04
_EXECUTABLE = 0x20000000
_PE64 = 0x20B


class PeImage(Image):
    """A 64-bit Windows DLL: MSVC RTTI, and .pdata for function entries."""

    def __init__(self, data: bytes, sections: list[Section], image_base: int) -> None:
        super().__init__(data, sections)
        self.image_base = image_base

    @classmethod
    def parse(cls, data: bytes) -> PeImage:
        (header,) = struct.unpack_from("<I", data, 0x3C)
        if data[:2] != b"MZ" or data[header : header + 4] != b"PE\0\0":
            raise VoltmodError("not a PE file")
        count, optional_size = struct.unpack_from("<H12xH", data, header + 6)
        magic, image_base = struct.unpack_from("<H22xQ", data, header + 24)
        if magic != _PE64:
            raise VoltmodError("not a 64-bit PE file")
        table = header + 24 + optional_size
        return cls(data, [_section(data, table + 40 * index) for index in range(count)], image_base)

    def vtables(self, class_name: str, subobject: int = 0) -> list[int]:
        return [
            table
            for locator in self._locators(class_name)
            if self._is_locator(locator, subobject)
            for table in self._tables_after(locator)
        ]

    def _locators(self, class_name: str) -> list[int]:
        """Every candidate complete object locator naming `class_name`'s type descriptor."""
        data, rdata = self.section(".data"), self.section(".rdata")
        if data is None or rdata is None:
            return []
        names = [f".?{kind}{class_name}@@".encode() + b"\0" for kind in ("AV", "AU")]
        descriptors = [at - _DESCRIPTOR_NAME for name in names for at in self.find_in(data, name)]
        return [
            field - _LOCATOR_DESCRIPTOR
            for descriptor in descriptors
            for field in self.find_in(rdata, struct.pack("<I", descriptor), 4)
        ]

    def _is_locator(self, locator: int, subobject: int) -> bool:
        head, own = self.read(locator, 8), self.read(locator + _LOCATOR_SELF, 4)
        if head is None or own is None:
            return False
        signature, offset = struct.unpack("<2I", head)
        return signature == 1 and offset == subobject and struct.unpack("<I", own)[0] == locator

    def _tables_after(self, locator: int) -> list[int]:
        """A table starts right after a pointer to its locator."""
        rdata = self.section(".rdata")
        pointer = struct.pack("<Q", self.image_base + locator)
        return [word + 8 for word in self.find_in(rdata, pointer, 8)] if rdata else []

    def _unwind_starts(self) -> set[int]:
        # A chained entry is a split-off part of another function, not a start.
        return {begin for begin, unwind in self._runtime_functions() if not self._chained(unwind)}

    def _runtime_functions(self) -> list[tuple[int, int]]:
        pdata = self.section(".pdata")
        if pdata is None:
            return []
        raw = self._bytes_of(pdata)
        entries = struct.iter_unpack("<3I", raw[: len(raw) - len(raw) % 12])
        return [(begin, unwind) for begin, _, unwind in entries if begin]

    def _chained(self, unwind: int) -> bool:
        head = self.read(unwind, 1)
        return head is not None and bool((head[0] >> 3) & _UNWIND_CHAINED)


def _section(data: bytes, at: int) -> Section:
    name = data[at : at + 8].rstrip(b"\0").decode("ascii", "replace")
    virtual_size, address, raw_size, offset = struct.unpack_from("<4I", data, at + 8)
    (flags,) = struct.unpack_from("<I", data, at + 36)
    size = min(virtual_size or raw_size, raw_size)
    return Section(name, address, size, offset, bool(flags & _EXECUTABLE))
