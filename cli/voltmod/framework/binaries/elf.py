import struct

from voltmod.errors import VoltmodError
from voltmod.framework.binaries.image import Image, Section

_EXECUTABLE = 0x4
_NOBITS = 8
# What GCC and Clang emit: a version, then a 4-byte frame pointer, count and datarel pair table.
_FRAME_INDEX_VERSION = 1
_UDATA4 = 0x03
_DATAREL_SDATA4 = 0x3B


class ElfImage(Image):
    """A 64-bit x86 Linux shared object: Itanium RTTI, and .eh_frame_hdr for function entries."""

    @classmethod
    def parse(cls, data: bytes) -> ElfImage:
        if data[:4] != b"\x7fELF" or data[4] != 2:
            raise VoltmodError("not a 64-bit ELF file")
        (table,) = struct.unpack_from("<Q", data, 0x28)
        size, count, names_index = struct.unpack_from("<3H", data, 0x3A)
        headers = [struct.unpack_from("<IIQQQQ", data, table + size * i) for i in range(count)]
        names = headers[names_index][4]
        sections = [_section(data, names, header) for header in headers]
        return cls(data, [section for section in sections if section is not None])

    def vtables(self, class_name: str, subobject: int = 0) -> list[int]:
        # A table's offset-to-top is minus its subobject, and its first slot holds code.
        offset_to_top = struct.pack("<q", -subobject)
        return [
            table
            for typeinfo in self._typeinfos(class_name)
            for table in self._tables_of(typeinfo)
            if self.read(table - 16, 8) == offset_to_top and self.is_code(self.pointer(table))
        ]

    def _typeinfos(self, class_name: str) -> list[int]:
        """Each typeinfo {vptr, name} whose name is `class_name`, length-prefixed."""
        name = f"{len(class_name)}{class_name}".encode() + b"\0"
        names = [
            at
            for section in self.sections
            if not section.executable
            for at in self.find_in(section, name)
            if not self._inside_longer_name(at)
        ]
        return [field - 8 for at in names for field in self._pointers_to(at)]

    def _tables_of(self, typeinfo: int) -> list[int]:
        """Slot 0 of each table whose header points at `typeinfo`."""
        return [field + 8 for field in self._pointers_to(typeinfo)]

    def _inside_longer_name(self, at: int) -> bool:
        before = self.read(at - 1, 1)
        return before is not None and (before.isalnum() or before == b"_")

    def _pointers_to(self, target: int) -> list[int]:
        relro = self.section(".data.rel.ro")
        return self.find_in(relro, struct.pack("<Q", target), 8) if relro else []

    def _unwind_starts(self) -> set[int]:
        header = self.section(".eh_frame_hdr")
        return set(read_frame_index(self.data, header)) if header else set()


def read_frame_index(data: bytes, header: Section) -> list[int]:
    """The function entries in the .eh_frame_hdr search table."""
    raw = data[header.offset : header.offset + header.size]
    version, frame_pointer, count_format, table_format = raw[:4]
    supported = (
        version == _FRAME_INDEX_VERSION
        and frame_pointer & 0x0F in (_UDATA4, 0x0B)
        and count_format == _UDATA4
        and table_format == _DATAREL_SDATA4
    )
    if not supported:
        raise VoltmodError(f"unsupported .eh_frame_hdr layout {raw[:4].hex()}")
    (count,) = struct.unpack_from("<I", raw, 8)
    pairs = struct.iter_unpack("<ii", raw[12 : 12 + 8 * count])
    return [header.address + begin for begin, _ in pairs]


def _section(data: bytes, names: int, header: tuple[int, ...]) -> Section | None:
    """The section a header describes, or None when it is not loaded from the file."""
    name_at, kind, flags, address, offset, length = header
    if not address or kind == _NOBITS:
        return None
    name_end = data.index(b"\0", names + name_at)
    name = data[names + name_at : name_end].decode("ascii", "replace")
    return Section(name, address, length, offset, bool(flags & _EXECUTABLE))
