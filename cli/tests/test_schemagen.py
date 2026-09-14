"""Cover the schema generator: resolve rules on a synthetic dump, output on the committed tree."""

import json
from pathlib import Path

import pytest

from voltmod.errors import VoltmodError
from voltmod.schemagen.generate import (
    BASELINES,
    GENERATED_HEADER_DIR,
    GENERATED_SOURCE_DIR,
    MANIFEST,
    render_outputs,
    write_outputs,
)
from voltmod.schemagen.model import accessor_name
from voltmod.schemagen.resolve import baseline_dump, collect_enums, resolve_classes

REPO_ROOT = Path(__file__).resolve().parents[2]


def type_info(name, category="SCHEMA_TYPE_BUILTIN", **extra):
    return {"name": name, "category": category, **extra}


def dumped_field(name, offset, size, info, networked=True):
    return {"name": name, "offset": offset, "size": size, "type": info, "networked": networked}


def dump():
    """An entity, a chained component, an embedded struct and the types they reach."""
    return {
        "classes": {
            "CEntityInstance": {"size": 48, "base": "", "chain_offset": -1, "fields": []},
            "CBaseEntity": {
                "size": 1192,
                "base": "CEntityInstance",
                "chain_offset": -1,
                "fields": [
                    dumped_field("m_iHealth", 720, 4, type_info("int32")),
                    dumped_field("m_lifeState", 728, 1, type_info("uint8")),
                    dumped_field(
                        "m_MoveType", 755, 1,
                        type_info("MoveType_t", "SCHEMA_TYPE_DECLARED_ENUM"),
                    ),
                    dumped_field(
                        "m_hGroundEntity", 1004, 4,
                        type_info(
                            "CHandle< CBaseEntity >",
                            "SCHEMA_TYPE_ATOMIC",
                            atomic="SCHEMA_ATOMIC_T",
                            inner="CBaseEntity",
                        ),
                    ),
                    dumped_field(
                        "m_pServices", 40, 8,
                        type_info("CMoneyServices*", "SCHEMA_TYPE_POINTER", inner="CMoneyServices"),
                    ),
                    dumped_field(
                        "m_state", 300, 8,
                        type_info("CEmbedded", "SCHEMA_TYPE_DECLARED_CLASS"),
                    ),
                    dumped_field(
                        "m_vecStuff", 400, 24,
                        type_info(
                            "CUtlVector< int >",
                            "SCHEMA_TYPE_ATOMIC",
                            atomic="SCHEMA_ATOMIC_COLLECTION_OF_T",
                            inner="int32",
                        ),
                    ),
                    dumped_field(
                        "m_szName", 500, 32,
                        type_info("char[32]", "SCHEMA_TYPE_FIXED_ARRAY", inner="char", extent=32),
                    ),
                    dumped_field(
                        "m_nSlots", 600, 20,
                        type_info("int32[5]", "SCHEMA_TYPE_FIXED_ARRAY", inner="int32", extent=5),
                    ),
                    dumped_field("m_bits", 700, 4, type_info("bitfield:3", "SCHEMA_TYPE_BITFIELD")),
                ],
            },
            "CMoneyServices": {
                "size": 88,
                "base": "",
                "chain_offset": 8,
                "fields": [dumped_field("m_iAccount", 72, 4, type_info("int32"))],
            },
            "CEmbedded": {
                "size": 8,
                "base": "",
                "chain_offset": -1,
                "fields": [dumped_field("m_bFlag", 4, 1, type_info("bool"))],
            },
            "CLonely": {
                "size": 16,
                "base": "",
                "chain_offset": -1,
                "fields": [dumped_field("m_iValue", 0, 4, type_info("int32"))],
            },
        },
        "enums": {
            "MoveType_t": {
                "size": 1,
                "items": [
                    {"name": "MOVETYPE_NONE", "value": 0},
                    {"name": "MOVETYPE_WALK", "value": 2},
                ],
            },
            "Unused_t": {"size": 4, "items": [{"name": "UNUSED", "value": 0}]},
        },
    }


def manifest(classes=None):
    return {
        "classes": classes
        if classes is not None
        else {
            "CBaseEntity": [
                "m_iHealth>Health",
                "m_lifeState",
                "m_MoveType",
                "m_hGroundEntity",
                "m_pServices>Services",
                "m_state>State",
                "m_vecStuff>Stuff",
                "m_szName>Name",
                "m_nSlots>Slots",
                "m_bits",
            ],
            "CMoneyServices": ["m_iAccount>Account"],
            "CEmbedded": ["m_bFlag>Flag"],
        },
        "wrappers": {"Entity": ["CBaseEntity"]},
    }


def class_header(dumped, selected, name):
    return render_outputs(dumped, selected, "windows").files[GENERATED_HEADER_DIR / f"{name}.hpp"]


@pytest.mark.parametrize("platform", list(BASELINES))
def test_the_committed_generated_tree_is_what_the_generator_writes(platform):
    """The committed baseline and manifest must regenerate every committed file byte for byte."""
    baseline = json.loads((REPO_ROOT / BASELINES[platform]).read_text(encoding="utf-8"))
    shipped = json.loads((REPO_ROOT / MANIFEST).read_text(encoding="utf-8"))
    files = render_outputs(baseline, shipped, platform).files
    write_outputs(REPO_ROOT, files, platform, check=True)


def test_the_closure_pulls_in_bases_and_returned_types_but_nothing_else():
    classes = resolve_classes(dump(), manifest())
    assert "CEntityInstance" in classes, "a base must be generated so the C++ chain matches"
    assert "CMoneyServices" in classes and "CEmbedded" in classes
    assert "CLonely" not in classes


def test_only_what_the_generator_read_reaches_the_baseline():
    dumped = dump()
    classes = resolve_classes(dumped, manifest())
    enums = collect_enums(dumped, classes)

    baseline = baseline_dump(dumped, classes, enums)
    assert set(baseline["classes"]) == set(classes)
    assert set(baseline["enums"]) == {"MoveType_t"}


def test_an_unmapped_type_is_skipped_visibly():
    header = class_header(dump(), manifest(), "CBaseEntity")
    assert "// skipped: m_bits (bitfield:3)" in header


def test_a_struct_embedded_in_an_entity_keeps_its_setters():
    assert resolve_classes(dump(), manifest())["CEmbedded"].embeds_in_entity is True
    assert "void SetFlag(bool value) const" in class_header(dump(), manifest(), "CEmbedded")


def test_a_field_the_engine_does_not_network_is_written_without_a_notify():
    dumped = dump()
    dumped["classes"]["CBaseEntity"]["fields"][1]["networked"] = False
    source = render_outputs(dumped, manifest(), "windows").files[
        GENERATED_SOURCE_DIR / "windows" / "CBaseEntity.cpp"
    ]
    assert "*MemberPtr<uint8_t>(_base, kCBaseEntity_LifeState) = value;" in source
    assert "_ownerOffset + kCBaseEntity_LifeState" not in source
    assert "_ownerOffset + kCBaseEntity_Health" in source


def test_a_class_with_no_route_to_replicate_writes_is_read_only():
    dumped = dump()
    dumped["classes"]["CBaseEntity"]["base"] = ""
    header = class_header(dumped, manifest(), "CEmbedded")
    assert "bool Flag() const" in header
    assert "SetFlag" not in header


def test_a_type_override_reads_the_leading_value_of_a_larger_field():
    selected = manifest({"CBaseEntity": ["m_state>Offset:Vector"]})
    header = class_header(dump(), selected, "CBaseEntity")
    assert "Vector Offset() const" in header


def test_a_star_takes_every_field_the_dump_reports():
    classes = resolve_classes(dump(), manifest({"CMoneyServices": "*"}))
    assert [field.schema_name for field in classes["CMoneyServices"].fields] == ["m_iAccount"]


@pytest.mark.parametrize(
    ("schema_name", "expected"),
    [
        ("m_flVelocityModifier", "VelocityModifier"),
        ("m_iAccount", "Account"),
        ("m_bOnGroundLastTick", "OnGroundLastTick"),
        ("m_angEyeAngles", "EyeAngles"),
        ("m_ArmorValue", "ArmorValue"),
        ("m_lifeState", "LifeState"),
        ("m_modelState", "ModelState"),
    ],
)
def test_the_accessor_name_strips_only_a_real_hungarian_prefix(schema_name, expected):
    assert accessor_name(schema_name) == expected


def test_two_fields_mapping_to_one_accessor_are_refused():
    with pytest.raises(VoltmodError, match="both map to"):
        resolve_classes(dump(), manifest({"CBaseEntity": ["m_iHealth>Same", "m_lifeState>Same"]}))


def test_a_manifest_field_the_dump_lacks_is_refused():
    with pytest.raises(VoltmodError, match="m_iNope"):
        resolve_classes(dump(), manifest({"CBaseEntity": ["m_iNope"]}))
