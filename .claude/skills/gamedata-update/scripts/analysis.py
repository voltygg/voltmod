"""Vtable slots, new patterns and address drift between two builds."""

import bisect
import json
import struct
from pathlib import Path

from binary import Binary, Function, memory_operand

from voltmod.framework.game_builds import archive_dir, resolved_record
from voltmod.framework.gamedata import parse_gamedata, read_gamedata
from voltmod.platforms import Platform

FRAMEWORK = Path(__file__).resolve().parents[4]


def vtable(binary: Binary, class_name: str, subobject: int = 0) -> int:
    """RVA of slot 0 of the class's table at `subobject`; 0 is the primary table."""
    tables = binary.image.vtables(class_name, subobject)
    if len(tables) != 1:
        raise LookupError(f"{len(tables)} {class_name} tables at offset {subobject}, not one")
    return tables[0]


def slots(binary: Binary, class_name: str, subobject: int = 0) -> list[int]:
    """Every slot's code address, up to the end of the table."""
    return binary.image.slots(vtable(binary, class_name, subobject))


def print_slots(binary: Binary, class_name: str, first: int, last: int) -> None:
    """One line per slot: index, address, size, calls, strings, first instructions."""
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
    """The shortest pattern of at least `min_length` bytes matching only the function at `start`.

    Branch targets, rip-relative displacements and struct offsets of 0x100 or more become
    wildcards, since those drift between builds. Raise `min_length` to reach a tail that tells
    twin functions apart.
    """
    tokens: list[str] = []
    for insn in binary.instructions(start, 256):
        tokens += _pattern_tokens(insn)
        if insn.address + insn.size - start < min_length:
            continue
        pattern = " ".join(tokens).rstrip(" ?")
        if binary.find(pattern) == [start]:
            return pattern
    return None


def load_gamedata() -> dict:
    return parse_gamedata(read_gamedata(FRAMEWORK))


def load_resolved(build: str, platform: str) -> dict | None:
    """The host's resolved record that `gamedata fetch` archived beside a build."""
    path = resolved_record(archive_dir() / build / platform, Platform(platform))
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


def _pattern_tokens(insn) -> list[str]:
    wildcards = _branch_target_bytes(insn) | _displacement_bytes(insn)
    return ["?" if index in wildcards else f"{byte:02X}" for index, byte in enumerate(insn.bytes)]


def _branch_target_bytes(insn) -> set[int]:
    is_branch = insn.mnemonic == "call" or insn.mnemonic.startswith("j")
    if not (is_branch and insn.op_str.startswith("0x")):
        return set()
    width = 4 if insn.size >= 5 else 1
    return set(range(insn.size - width, insn.size))


def _displacement_bytes(insn) -> set[int]:
    operand = memory_operand(insn)
    if not operand or (operand[0] != "rip" and abs(operand[1]) < 0x100):
        return set()
    at = bytes(insn.bytes).find(struct.pack("<i", operand[1]))
    return set(range(at, at + 4)) if at >= 0 else set()
