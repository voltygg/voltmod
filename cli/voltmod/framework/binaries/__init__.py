"""Server and engine2 binaries read from disk: sections, RTTI vtables and function entries."""

from voltmod.errors import VoltmodError
from voltmod.framework.binaries.elf import ElfImage, read_frame_index
from voltmod.framework.binaries.image import Image, Section, padding_ends
from voltmod.framework.binaries.pe import PeImage

__all__ = [
    "ElfImage",
    "Image",
    "PeImage",
    "Section",
    "padding_ends",
    "parse_image",
    "read_frame_index",
]


def parse_image(data: bytes) -> Image:
    if data[:2] == b"MZ":
        return PeImage.parse(data)
    if data[:4] == b"\x7fELF":
        return ElfImage.parse(data)
    raise VoltmodError("neither a PE nor an ELF file")
