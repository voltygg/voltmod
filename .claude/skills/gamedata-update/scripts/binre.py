# /// script
# dependencies = ["capstone", "pefile", "pyelftools", "numpy"]
# ///
"""Helpers for finding gamedata entries in CS2 server binaries after an update.

Write a small snippet file and run it with these helpers in scope:

    uv run .claude/skills/gamedata-update/scripts/binre.py snippet.py

    # snippet.py
    new = Binary.open("2000913", "windows")          # a build fetched by `voltmod gamedata fetch`
    print(new.disasm(new.find("48 89 5C 24 08")[0]))

Every address is an RVA, the same numbers resolved.<platform>.json records.
"""

import bisect
import functools
import io
import json
import re
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

import capstone
import numpy as np

FRAMEWORK = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(FRAMEWORK / "cli"))

from voltmod.framework.game_builds import default_archive, resolved_record  # noqa: E402
from voltmod.framework.gamedata import (  # noqa: E402
    module_path,
    parse_gamedata,
    pattern_regex,
    read_gamedata,
)

PE_EXECUTABLE = 0x20000000  # IMAGE_SCN_MEM_EXECUTE
ELF_EXECUTABLE = 0x4  # SHF_EXECINSTR


@dataclass(frozen=True)
class Section:
    name: str
    rva: int
    size: int
    file_offset: int
    executable: bool

    def holds(self, rva: int) -> bool:
        return self.rva <= rva < self.rva + self.size


@dataclass(frozen=True, order=True)
class Function:
    start: int
    end: int

    @property
    def size(self) -> int:
        return self.end - self.start


class Binary:
    """One server or engine2 image read from disk."""

    def __init__(self, path: Path, platform: str) -> None:
        self.path = Path(path)
        self.platform = platform
        self.data = self.path.read_bytes()
        self.image_base, self.sections = _read_sections(self.data, platform)
        self.disassembler = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

    @classmethod
    def open(cls, build: str, platform: str, module: str = "server") -> Binary:
        """A module from the build archive, or from any game directory passed as `build`."""
        root = Path(build) if Path(build).is_dir() else default_archive() / build / platform
        return cls(module_path(root, platform, module), platform)

    def section(self, name: str) -> Section:
        return next(section for section in self.sections if section.name == name)

    def code_sections(self) -> list[Section]:
        return [section for section in self.sections if section.executable]

    def is_code(self, rva: int) -> bool:
        return any(section.holds(rva) for section in self.code_sections())

    def file_offset(self, rva: int) -> int | None:
        for section in self.sections:
            if section.holds(rva):
                return section.file_offset + rva - section.rva
        return None

    def rva(self, file_offset: int) -> int | None:
        for section in self.sections:
            if section.file_offset <= file_offset < section.file_offset + section.size:
                return section.rva + file_offset - section.file_offset
        return None

    def read(self, rva: int, length: int) -> bytes:
        offset = self.file_offset(rva)
        return b"" if offset is None else self.data[offset : offset + length]

    def pointer(self, rva: int) -> int:
        """The 8-byte pointer stored at `rva`, as an RVA."""
        return struct.unpack("<Q", self.read(rva, 8))[0] - self.image_base

    def find(self, pattern: str) -> list[int]:
        """Where a gamedata byte pattern matches."""
        return [self.rva(match.start()) for match in pattern_regex(pattern).finditer(self.data)]

    def find_string(self, text: str) -> list[int]:
        needle = re.escape(text.encode() + b"\0")
        return [self.rva(match.start()) for match in re.finditer(needle, self.data)]

    def string_at(self, rva: int) -> str | None:
        """The printable C string at `rva`, if one starts there."""
        raw = self.read(rva, 300).split(b"\0", 1)[0]
        return raw.decode() if len(raw) >= 4 and all(32 <= byte < 127 for byte in raw) else None

    def references_to(self, target: int) -> list[int]:
        """Addresses of every rip-relative displacement pointing at `target`."""
        found = []
        for section in self.code_sections():
            # Four strided int32 views cover every byte offset without widening the section.
            for phase in range(4):
                count = (section.size - phase) // 4
                offset = section.file_offset + phase
                displacements = np.frombuffer(self.data, "<i4", count, offset)
                positions = section.rva + phase + 4 * np.arange(count, dtype=np.int64)
                # The displacement ends the instruction, or an 8-bit immediate follows it.
                for tail in (4, 5):
                    found += positions[positions + tail + displacements == target].tolist()
        return sorted(found)

    def calls_to(self, target: int) -> list[int]:
        """Direct `call` and `jmp` instructions targeting `target`."""
        return [
            ref - 1
            for ref in self.references_to(target)
            if self.read(ref - 1, 1) in (b"\xe8", b"\xe9")
        ]

    def instructions(self, start: int, length: int):
        return list(self.disassembler.disasm(self.read(start, length), start))

    def disasm(self, rva: int, count: int = 40) -> str:
        lines = []
        for insn in self.instructions(rva, count * 15)[:count]:
            line = f"  {insn.address:#x}: {insn.bytes.hex(' '):<30} {insn.mnemonic} {insn.op_str}"
            target = rip_target(insn)
            if target is not None:
                line += f"  -> {target:#x} {self.string_at(target) or ''}"
            lines.append(line)
        return "\n".join(lines)

    def function_at(self, rva: int) -> Function | None:
        """The function containing `rva`; it ends where the next function starts."""
        starts = self.function_starts
        index = bisect.bisect_right(starts, rva) - 1
        if index < 0 or index + 1 >= len(starts):
            return None
        return Function(starts[index], starts[index + 1])

    def calls_in(self, function: Function) -> list[int]:
        """Direct call and jump targets outside the function, in order."""
        targets = [
            int(insn.op_str, 16)
            for insn in self.instructions(function.start, function.size)
            if insn.mnemonic in ("call", "jmp") and insn.op_str.startswith("0x")
        ]
        return [target for target in targets if not function.start <= target < function.end]

    def strings_in(self, function: Function) -> list[str]:
        found = []
        for insn in self.instructions(function.start, function.size):
            target = rip_target(insn)
            text = self.string_at(target) if target is not None else None
            if text:
                found.append(text)
        return found

    def callers_of(self, target: int) -> list[Function]:
        return sorted({self.function_at(site) for site in self.calls_to(target)} - {None})

    def functions_using(self, text: str) -> list[Function]:
        """Functions that reference a string, the usual anchor for re-finding a function."""
        found = {
            self.function_at(ref)
            for rva in self.find_string(text)
            for ref in self.references_to(rva)
        }
        return sorted(found - {None})

    @functools.cached_property
    def function_starts(self) -> list[int]:
        return sorted(_function_starts(self))


def rip_target(insn) -> int | None:
    """The address a `[rip + disp]` operand points at."""
    operand = _memory_operand(insn)
    if operand is None or operand[0] != "rip":
        return None
    return insn.address + insn.size + operand[1]


def _memory_operand(insn) -> tuple[str, int] | None:
    """The base register and signed displacement of a `[base + index*scale +- disp]` operand."""
    match = re.search(r"\[(\w+)(?: \+ \w+\*\d)? ([+-]) (0x[0-9a-f]+)\]", insn.op_str)
    if not match:
        return None
    return match.group(1), int(match.group(3), 16) * (-1 if match.group(2) == "-" else 1)


def vtable(binary: Binary, class_name: str, subobject: int = 0) -> int:
    """RVA of slot 0 of the class's table at `subobject` (0 is the primary table)."""
    tables = (
        _vtables_windows(binary, class_name)
        if binary.platform == "windows"
        else _vtables_linux(binary, class_name)
    )
    if subobject not in tables:
        raise LookupError(f"no {class_name} table at offset {subobject}; found {sorted(tables)}")
    return tables[subobject]


def slots(binary: Binary, class_name: str, subobject: int = 0) -> list[int]:
    """Every slot's code address, up to the end of the table."""
    table = vtable(binary, class_name, subobject)
    addresses = []
    while binary.is_code(address := binary.pointer(table + 8 * len(addresses))):
        addresses.append(address)
    return addresses


def print_slots(binary: Binary, class_name: str, first: int, last: int) -> None:
    """A slot per line: index, address, size, calls, strings, first instructions."""
    addresses = slots(binary, class_name)
    for index in range(first, min(last + 1, len(addresses))):
        address = addresses[index]
        function = binary.function_at(address) or Function(address, address)
        calls = len(binary.calls_in(function))
        strings = binary.strings_in(function)[:2]
        head = " ; ".join(f"{i.mnemonic} {i.op_str}" for i in binary.instructions(address, 24)[:3])
        print(f"  [{index}] {address:#x} size {function.size:5} calls {calls:2} {strings} | {head}")
    print(f"  {class_name}: {len(addresses)} slots")


def make_pattern(binary: Binary, start: int, min_length: int = 16) -> str | None:
    """The shortest pattern, at least `min_length` bytes, matching only the function at `start`.

    Branch targets, rip-relative displacements and struct offsets of 0x100 or more become
    wildcards, since those drift between builds. Raise `min_length` to reach a tail that tells
    twin functions apart.
    """
    tokens = []
    for insn in binary.instructions(start, 256):
        tokens += _pattern_tokens(insn)
        length = insn.address + insn.size - start
        if length >= min_length:
            pattern = " ".join(tokens).rstrip(" ?")
            if binary.find(pattern) == [start]:
                return pattern
    return None


def _pattern_tokens(insn) -> list[str]:
    wildcards = set()
    if insn.op_str.startswith("0x") and (insn.mnemonic == "call" or insn.mnemonic.startswith("j")):
        width = 4 if insn.size >= 5 else 1
        wildcards.update(range(insn.size - width, insn.size))
    operand = _memory_operand(insn)
    if operand and (operand[0] == "rip" or abs(operand[1]) >= 0x100):
        encoded = struct.pack("<i", operand[1])
        at = bytes(insn.bytes).find(encoded)
        if at >= 0:
            wildcards.update(range(at, at + 4))
    return ["?" if index in wildcards else f"{byte:02X}" for index, byte in enumerate(insn.bytes)]


def load_gamedata() -> dict:
    return parse_gamedata(read_gamedata(FRAMEWORK))


def load_resolved(build: str, platform: str) -> dict | None:
    """The host's resolved record that `voltmod gamedata fetch` archived beside a build."""
    path = resolved_record(default_archive() / build / platform, platform)
    return json.loads(path.read_text(encoding="utf-8")) if path.is_file() else None


def drift(anchors: list[tuple[int, int]]):
    """old address -> expected new address, interpolated between known (old, new) pairs."""
    anchors = sorted(anchors)
    olds = [old for old, _ in anchors]

    def expected(old: int) -> int:
        index = min(max(bisect.bisect_right(olds, old) - 1, 0), len(anchors) - 2)
        (old0, new0), (old1, new1) = anchors[index], anchors[index + 1]
        shift0, shift1 = new0 - old0, new1 - old1
        return old + shift0 + int((shift1 - shift0) * (old - old0) / (old1 - old0))

    return expected


def _read_sections(data: bytes, platform: str) -> tuple[int, list[Section]]:
    if platform == "windows":
        import pefile

        pe = pefile.PE(data=data, fast_load=True)
        sections = [
            Section(
                name=s.Name.rstrip(b"\0").decode(),
                rva=s.VirtualAddress,
                size=min(s.Misc_VirtualSize, s.SizeOfRawData),
                file_offset=s.PointerToRawData,
                executable=bool(s.Characteristics & PE_EXECUTABLE),
            )
            for s in pe.sections
        ]
        return pe.OPTIONAL_HEADER.ImageBase, sections

    from elftools.elf.elffile import ELFFile

    sections = [
        Section(
            name=s.name,
            rva=s["sh_addr"],
            size=s["sh_size"],
            file_offset=s["sh_offset"],
            executable=bool(s["sh_flags"] & ELF_EXECUTABLE),
        )
        for s in ELFFile(io.BytesIO(data)).iter_sections()
        if s["sh_addr"] and s["sh_type"] != "SHT_NOBITS"
    ]
    return 0, sections


def _function_starts(binary: Binary) -> set[int]:
    """Function entry points: unwind tables, plus the aligned address after each int3 padding run.

    Windows .pdata lists nearly every function; its chained entries are fragments of another
    function and are skipped. Linux builds give most functions no unwind entry, so the padding
    between functions does most of the work there.
    """
    starts = set()
    if binary.platform == "windows":
        pdata = binary.section(".pdata")
        table = binary.data[pdata.file_offset : pdata.file_offset + pdata.size]
        for begin, _, unwind in struct.iter_unpack("<III", table[: len(table) // 12 * 12]):
            is_fragment = (binary.read(unwind, 1)[0] >> 3) & 0x4  # UNW_FLAG_CHAININFO
            if begin and not is_fragment:
                starts.add(begin)
    else:
        header = binary.section(".eh_frame_hdr")
        count = struct.unpack_from("<I", binary.data, header.file_offset + 8)[0]
        for index in range(count):
            entry = header.file_offset + 12 + 8 * index
            starts.add(header.rva + struct.unpack_from("<i", binary.data, entry)[0])

    for section in binary.code_sections():
        raw = np.frombuffer(binary.data, np.uint8, section.size, section.file_offset)
        after_padding = np.nonzero((raw[:-1] == 0xCC) & (raw[1:] != 0xCC))[0] + 1 + section.rva
        starts.update(after_padding[after_padding % 16 == 0].tolist())
    return starts


def _vtables_windows(binary: Binary, class_name: str) -> dict[int, int]:
    """MSVC RTTI: type descriptor -> complete object locators -> the table after each locator."""
    rdata = binary.section(".rdata")
    blob = binary.data[rdata.file_offset : rdata.file_offset + rdata.size]
    name_at = binary.data.find(f".?AV{class_name}@@".encode() + b"\0")
    if name_at < 0:
        return {}
    descriptor = binary.rva(name_at) - 0x10
    tables = {}
    for match in re.finditer(re.escape(struct.pack("<I", descriptor)), blob):
        locator_at = match.start() - 12
        locator = rdata.rva + locator_at
        signature, offset, _, _, _, self_rva = struct.unpack_from("<IIIIII", blob, locator_at)
        if signature != 1 or self_rva != locator:
            continue
        pointer_at = blob.find(struct.pack("<Q", binary.image_base + locator))
        if pointer_at >= 0:
            tables[offset] = rdata.rva + pointer_at + 8
    return tables


def _vtables_linux(binary: Binary, class_name: str) -> dict[int, int]:
    """Itanium RTTI: mangled name -> typeinfo -> each table whose header points at that typeinfo."""
    relro = binary.section(".data.rel.ro")
    blob = binary.data[relro.file_offset : relro.file_offset + relro.size]
    tables = {}
    for name in re.finditer(
        re.escape(f"{len(class_name)}{class_name}".encode() + b"\0"), binary.data
    ):
        if binary.data[name.start() - 1] != 0:
            continue
        for name_pointer in re.finditer(
            re.escape(struct.pack("<Q", binary.rva(name.start()))), blob
        ):
            typeinfo = relro.rva + name_pointer.start() - 8
            for header in re.finditer(re.escape(struct.pack("<Q", typeinfo)), blob):
                offset_to_top = struct.unpack_from("<q", blob, header.start() - 8)[0]
                tables.setdefault(-offset_to_top, relro.rva + header.start() + 8)
    return tables


if __name__ == "__main__":
    snippet = Path(sys.argv[1])
    exec(compile(snippet.read_text(encoding="utf-8"), str(snippet), "exec"), globals())
