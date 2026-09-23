import struct

from voltmod.framework.binaries import (
    ElfImage,
    PeImage,
    Section,
    padding_ends,
    read_frame_index,
)

PE_BASE = 0x180000000
CODE = Section(".text", 0x1000, 0x100, 0, executable=True)


def image_bytes(size: int, writes: dict[int, bytes]) -> bytes:
    data = bytearray(size)
    for offset, value in writes.items():
        data[offset : offset + len(value)] = value
    return bytes(data)


def words(*values: int) -> bytes:
    return struct.pack(f"<{len(values)}q", *values)


def test_function_entries_are_the_aligned_addresses_after_int3_padding():
    code = b"\xc3" + b"\xcc" * 15 + b"\x90" * 8 + b"\xcc" + b"\x90" * 23

    assert padding_ends(code, 0x1000) == {0x1010}, "0x1019 follows padding but is unaligned"


def test_the_frame_index_lists_entries_relative_to_its_own_address():
    header = Section(".eh_frame_hdr", 0x4000, 28, 0, executable=False)
    raw = bytes([1, 0x1B, 0x03, 0x3B]) + struct.pack("<iI4i", 0, 2, -0x3000, 0, -0x2F00, 0)

    assert read_frame_index(raw, header) == [0x1000, 0x1100]


def test_an_elf_vtable_slot_reads_the_target_the_file_stores():
    data = Section(".data.rel.ro", 0x2000, 0x20, 0x100, executable=False)
    contents = image_bytes(0x200, {0x100: words(0x1010, 0x1020, 0x3000)})
    image = ElfImage(contents, [CODE, data])

    assert image.pointer(0x2000) == 0x1010
    assert image.pointer(0x2018) is None, "a zero word points nowhere"
    assert image.slots(0x2000) == [0x1010, 0x1020], "stops at the first pointer off code"


def test_an_elf_vtable_is_found_through_its_typeinfo_name_and_subobject():
    rodata = Section(".rodata", 0x3000, 0x10, 0x100, executable=False)
    relro = Section(".data.rel.ro", 0x2000, 0x80, 0x200, executable=False)
    typeinfo = words(0x5000, 0x3000)  # {vptr, name}
    primary = words(0, 0x2000, 0x1010)  # offset-to-top, typeinfo, slot 0
    secondary = words(-8, 0x2000, 0x1020)
    contents = image_bytes(
        0x300, {0x100: b"3Foo\0", 0x200: typeinfo, 0x240: primary, 0x260: secondary}
    )
    image = ElfImage(contents, [CODE, rodata, relro])

    assert image.vtables("Foo") == [0x2050]
    assert image.vtables("Foo", 8) == [0x2070], "a base at 8 has offset-to-top -8"
    assert image.vtables("Fo") == []


def test_a_pe_vtable_follows_the_complete_object_locator_of_its_type_descriptor():
    data = Section(".data", 0x5000, 0x20, 0x100, executable=False)
    rdata = Section(".rdata", 0x6000, 0x40, 0x200, executable=False)
    locator = struct.pack("<6I", 1, 0, 0, 0x5000, 0, 0x6000)
    table = struct.pack("<3Q", PE_BASE + 0x6000, PE_BASE + 0x1010, PE_BASE + 0x1020)
    contents = image_bytes(0x300, {0x110: b".?AVFoo@@\0", 0x200: locator, 0x220: table})
    image = PeImage(contents, [CODE, data, rdata], PE_BASE)

    assert image.vtables("Foo") == [0x6028]
    assert image.slots(0x6028) == [0x1010, 0x1020]


def test_a_pe_chunk_that_chains_to_another_function_is_not_a_function_start():
    pdata = Section(".pdata", 0x7000, 0x18, 0x100, executable=False)
    unwind = Section(".xdata", 0x8000, 0x20, 0x200, executable=False)
    # Version 1 with the chain flag and no unwind codes, then the entry it continues.
    chained = bytes([1 | 0x04 << 3, 0, 0, 0]) + struct.pack("<3I", 0x1000, 0x1040, 0x8000)
    contents = image_bytes(
        0x300,
        {
            0x100: struct.pack("<6I", 0x1000, 0x1040, 0x8000, 0x1080, 0x10A0, 0x8010),
            0x200: bytes([1, 0, 0, 0]),
            0x210: chained,
        },
    )
    image = PeImage(contents, [CODE, pdata, unwind], PE_BASE)

    assert 0x1000 in image.function_starts
    assert 0x1080 not in image.function_starts, "the chunk at 0x1080 continues 0x1000"
