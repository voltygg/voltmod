"""A game binary with disassembly on top of the CLI's `voltmod.framework.binaries`."""

import bisect
import re
from dataclasses import dataclass
from pathlib import Path

import capstone
import numpy as np

from voltmod.framework.binaries import Image, Section, parse_image
from voltmod.framework.game_builds import archive_dir
from voltmod.framework.gamedata import module_path, pattern_regex
from voltmod.platforms import Platform

_CALL_OR_JMP = (b"\xe8", b"\xe9")
_MEMORY_OPERAND = re.compile(r"\[(\w+)(?: \+ \w+\*\d)? ([+-]) (0x[0-9a-f]+)\]")


@dataclass(frozen=True, order=True)
class Function:
    start: int
    end: int

    @property
    def size(self) -> int:
        return self.end - self.start


class Binary:
    """One server or engine2 module; every address is an RVA, as in resolved.<platform>.json."""

    def __init__(self, path: Path, platform: str) -> None:
        self.path = Path(path)
        self.platform = platform
        self.image: Image = parse_image(self.path.read_bytes())
        self.disassembler = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

    @classmethod
    def open(cls, build: str, platform: str, module: str = "server") -> Binary:
        """A module from the build archive, or from any game directory passed as `build`."""
        root = Path(build) if Path(build).is_dir() else archive_dir() / build / platform
        return cls(module_path(root, Platform(platform), module), platform)

    def read(self, rva: int, length: int) -> bytes:
        """Up to `length` bytes, cut at the end of the section holding `rva`."""
        section = next((each for each in self.image.sections if each.holds(rva)), None)
        if section is None:
            return b""
        return self.image.read(rva, min(length, section.address + section.size - rva)) or b""

    def find(self, pattern: str) -> list[int]:
        """Where a gamedata byte pattern matches in code."""
        regex = pattern_regex(pattern)
        data = self.image.data
        return [
            section.address + match.start() - section.offset
            for section in self.image.code_sections()
            for match in regex.finditer(data, section.offset, section.offset + section.size)
        ]

    def find_string(self, text: str) -> list[int]:
        return self.image.string_addresses(text)

    def string_at(self, rva: int) -> str | None:
        """The printable C string starting at `rva`, if there is one."""
        raw = self.read(rva, 300).split(b"\0", 1)[0]
        return raw.decode() if len(raw) >= 4 and all(32 <= byte < 127 for byte in raw) else None

    def references_to(self, target: int) -> list[int]:
        """Every rip-relative displacement pointing at `target`, in any instruction."""
        found: list[int] = []
        for section in self.image.code_sections():
            for phase in range(4):
                positions, displacements = self._displacements(section, phase)
                ends = positions + displacements
                # The displacement ends the instruction, or a one-byte immediate follows it.
                found += positions[(ends + 4 == target) | (ends + 5 == target)].tolist()
        return sorted(found)

    def calls_to(self, target: int) -> list[int]:
        """Direct `call` and `jmp` instructions to `target`."""
        sites = [ref - 1 for ref in self.references_to(target)]
        return [site for site in sites if self.read(site, 1) in _CALL_OR_JMP]

    def instructions(self, start: int, length: int):
        return list(self.disassembler.disasm(self.read(start, length), start))

    def disasm(self, rva: int, count: int = 40) -> str:
        return "\n".join(self._line(insn) for insn in self.instructions(rva, count * 15)[:count])

    def function_at(self, rva: int) -> Function | None:
        """The function holding `rva`; it ends where the next one starts."""
        starts = self.image.function_starts
        index = bisect.bisect_right(starts, rva) - 1
        if index < 0 or index + 1 >= len(starts):
            return None
        return Function(starts[index], starts[index + 1])

    def calls_in(self, function: Function) -> list[int]:
        """Direct call and jump targets outside `function`, in order."""
        targets = [
            int(insn.op_str, 16)
            for insn in self.instructions(function.start, function.size)
            if insn.mnemonic in ("call", "jmp") and insn.op_str.startswith("0x")
        ]
        return [target for target in targets if not function.start <= target < function.end]

    def strings_in(self, function: Function) -> list[str]:
        targets = (rip_target(insn) for insn in self.instructions(function.start, function.size))
        texts = (self.string_at(target) for target in targets if target is not None)
        return [text for text in texts if text]

    def callers_of(self, target: int) -> list[Function]:
        return _functions(self.function_at(site) for site in self.calls_to(target))

    def functions_using(self, text: str) -> list[Function]:
        """Functions referencing a string: the usual way back to a function after an update."""
        references = (ref for rva in self.find_string(text) for ref in self.references_to(rva))
        return _functions(self.function_at(ref) for ref in references)

    def _displacements(self, section: Section, phase: int):
        """Each int32 starting `phase` bytes into a 4-byte step, and where it sits."""
        count = (section.size - phase) // 4
        values = np.frombuffer(self.image.data, "<i4", count, section.offset + phase)
        positions = section.address + phase + 4 * np.arange(count, dtype=np.int64)
        return positions, values

    def _line(self, insn) -> str:
        line = f"  {insn.address:#x}: {insn.bytes.hex(' '):<30} {insn.mnemonic} {insn.op_str}"
        target = rip_target(insn)
        if target is not None:
            line += f"  -> {target:#x} {self.string_at(target) or ''}"
        return line


def rip_target(insn) -> int | None:
    """The address a `[rip + disp]` operand points at."""
    operand = memory_operand(insn)
    if operand is None or operand[0] != "rip":
        return None
    return insn.address + insn.size + operand[1]


def memory_operand(insn) -> tuple[str, int] | None:
    """The base register and signed displacement of a `[base + index*scale +- disp]` operand."""
    match = _MEMORY_OPERAND.search(insn.op_str)
    if not match:
        return None
    sign = -1 if match.group(2) == "-" else 1
    return match.group(1), sign * int(match.group(3), 16)


def _functions(found) -> list[Function]:
    return sorted({function for function in found if function is not None})
