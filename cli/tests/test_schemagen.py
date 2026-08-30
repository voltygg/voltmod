"""Cover the schema generator against a hand-built dump.

The generated code is committed, so a regression here ships as wrong offsets or a silently
missing accessor rather than as a build failure. These run against a tiny synthetic dump so a
CS2 update cannot make them fail for the wrong reason.
"""

import json
from pathlib import Path

import pytest
from voltmod.builder import schemagen


def builtin(name, category="builtin", **extra):
    return {"name": name, "category": category, **extra}


def field(name, offset, size, type_):
    return {"name": name, "offset": offset, "size": size, "type": type_}


def dump():
    """An entity, a chained component, an embedded struct and the types they reach."""
    return {
        "format": 1,
        "scopes": ["global", "server"],
        "classes": {
            "CEntityInstance": {"size": 48, "bases": [], "chain_offset": -1, "fields": []},
            "CBaseEntity": {
                "size": 1192,
                "bases": [{"name": "CEntityInstance", "offset": 0}],
                "chain_offset": -1,
                "fields": [
                    field("m_iHealth", 720, 4, builtin("int32")),
                    field("m_lifeState", 728, 1, builtin("uint8")),
                    field("m_MoveType", 755, 1, builtin("MoveType_t", "declared_enum",
                                                        declared="enum")),
                    field("m_hGroundEntity", 1004, 4,
                          builtin("CHandle< CBaseEntity >", "atomic", atomic="t",
                                  inner="CBaseEntity")),
                    field("m_pServices", 40, 8,
                          builtin("CMoneyServices*", "pointer", inner="CMoneyServices")),
                    field("m_state", 300, 8,
                          builtin("CEmbedded", "declared_class", declared="class")),
                    field("m_vecStuff", 400, 24,
                          builtin("CUtlVector< int >", "atomic", atomic="collection_of_t",
                                  inner="int32")),
                    field("m_szName", 500, 32,
                          builtin("char[32]", "fixed_array", inner="char", extent=32)),
                    field("m_nSlots", 600, 20,
                          builtin("int32[5]", "fixed_array", inner="int32", extent=5)),
                    field("m_bits", 700, 4, builtin("bitfield:3", "bitfield")),
                ],
            },
            "CMoneyServices": {
                "size": 88,
                "bases": [],
                "chain_offset": 8,
                "fields": [field("m_iAccount", 72, 4, builtin("int32"))],
            },
            "CEmbedded": {
                "size": 8,
                "bases": [],
                "chain_offset": -1,
                "fields": [field("m_bFlag", 4, 1, builtin("bool"))],
            },
            "CLonely": {
                "size": 16,
                "bases": [],
                "chain_offset": -1,
                "fields": [field("m_iValue", 0, 4, builtin("int32"))],
            },
        },
        "enums": {
            "MoveType_t": {"size": 1, "items": [{"name": "MOVETYPE_NONE", "value": 0},
                                                {"name": "MOVETYPE_WALK", "value": 2}]},
            "Unused_t": {"size": 4, "items": [{"name": "UNUSED", "value": 0}]},
        },
    }


def manifest(classes=None, wrappers=None):
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
        "wrappers": wrappers if wrappers is not None else {"Entity": ["CBaseEntity"]},
    }


def build(classes=None, wrappers=None):
    d = dump()
    return d, schemagen.build_classes(d, manifest(classes, wrappers))


def test_the_closure_pulls_in_bases_and_inner_types_but_not_the_rest():
    _, classes = build()
    assert "CEntityInstance" in classes, "a base must be generated so the C++ chain matches"
    assert "CMoneyServices" in classes and "CEmbedded" in classes
    assert "CLonely" not in classes


def test_only_the_enums_a_generated_field_returns_are_emitted():
    d, classes = build()
    enums = schemagen.collect_enums(d, classes)
    assert set(enums) == {"MoveType_t"}


def test_an_entity_notifies_itself_and_a_chained_component_uses_its_chainer():
    _, classes = build()
    entity = schemagen.emit_class_source(classes["CBaseEntity"])
    money = schemagen.emit_class_source(classes["CMoneyServices"])

    assert "NotifyEntity(_owner, _ownerOffset + kCBaseEntity_Health)" in entity
    assert "NotifyThroughChain(_base, CMoneyServices_kChainOffset" in money
    assert "NotifyEntity" not in money


def test_a_struct_embedded_in_an_entity_keeps_its_setters():
    _, classes = build()
    assert classes["CEmbedded"].embeds_in_entity is True
    assert "void SetFlag(bool value) const" in schemagen.emit_header(classes["CEmbedded"], classes)


def test_a_class_with_no_replication_route_is_read_only():
    """CEmbedded reached only from a non-entity holder has nowhere to send a write."""
    d = dump()
    d["classes"]["CBaseEntity"]["bases"] = []  # no longer an entity
    classes = schemagen.build_classes(d, manifest())
    assert classes["CEmbedded"].embeds_in_entity is False
    header = schemagen.emit_header(classes["CEmbedded"], classes)
    assert "bool Flag() const" in header
    assert "SetFlag" not in header


def test_an_embedded_view_carries_the_owner_so_a_deep_write_dirties_the_entity():
    _, classes = build()
    source = schemagen.emit_class_source(classes["CBaseEntity"])
    assert "CEmbedded{MemberPtr<void>(_base, kCBaseEntity_State), _owner," in source
    assert "_ownerOffset + kCBaseEntity_State}" in source


def test_a_pointer_field_starts_a_fresh_view_with_no_owner():
    _, classes = build()
    source = schemagen.emit_class_source(classes["CBaseEntity"])
    assert "CMoneyServices{*MemberPtr<void*>(_base, kCBaseEntity_Services)}" in source


def test_each_category_maps_to_the_intended_c_plus_plus_shape():
    _, classes = build()
    header = schemagen.emit_header(classes["CBaseEntity"], classes)
    assert "int32_t Health() const" in header
    assert "MoveType_t MoveType() const" in header
    assert "uint32_t GroundEntity() const" in header, "a CHandle reads as its raw handle"
    assert "std::string_view Name() const" in header
    assert "int32_t Slots(size_t index) const" in header
    assert "void* Stuff() const" in header, "a CUtlVector hands back its address"
    assert "// skipped: m_bits (bitfield:3)" in header


def test_the_offset_lands_in_the_source_and_never_in_the_header():
    _, classes = build()
    assert "720" not in schemagen.emit_header(classes["CBaseEntity"], classes)
    assert "static constexpr int32_t kCBaseEntity_Health = 720;" in schemagen.emit_class_source(
        classes["CBaseEntity"]
    )


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
    assert schemagen.accessor_name(schema_name) == expected


def test_two_fields_mapping_to_one_accessor_is_a_hard_error():
    d = dump()
    with pytest.raises(SystemExit, match="both map to"):
        schemagen.build_classes(
            d, manifest({"CBaseEntity": ["m_iHealth>Same", "m_lifeState>Same"]})
        )


def test_a_manifest_field_the_dump_does_not_have_is_a_hard_error():
    d = dump()
    with pytest.raises(SystemExit, match="m_iNope"):
        schemagen.build_classes(d, manifest({"CBaseEntity": ["m_iNope"]}))


def test_a_type_override_reads_the_leading_value_of_a_larger_field():
    _, classes = build({"CBaseEntity": ["m_state>Offset:Vector"]})
    header = schemagen.emit_header(classes["CBaseEntity"], classes)
    assert "Vector Offset() const" in header


def test_a_star_takes_every_field_the_dump_reports():
    _, classes = build({"CMoneyServices": "*"})
    assert [m.schema_name for m in classes["CMoneyServices"].members] == ["m_iAccount"]


def test_the_wrapper_fragment_forwards_through_a_view_over_the_wrappers_own_entity():
    _, classes = build()
    inc = schemagen.emit_wrapper("Entity", ["CBaseEntity"], classes)
    assert "int32_t Health() const { return Schema::CBaseEntity{_e}.Health(); }" in inc
    setter = "void SetHealth(int32_t value) const { Schema::CBaseEntity{_e}.SetHealth(value); }"
    assert setter in inc


def test_the_layout_table_names_the_arrays_each_class_defines():
    _, classes = build()
    layout = schemagen.emit_layout_source(classes)
    assert "extern const FieldLayout CBaseEntity_kFields[9];" in layout, "the bitfield is skipped"
    assert '{.Name = "CMoneyServices", .Size = 88, .ChainOffset = 8' in layout
    empty = '.Name = "CEntityInstance", .Size = 48, .ChainOffset = -1, .Fields = {}'
    assert empty in layout


def test_generating_twice_from_one_dump_gives_identical_text():
    """No timestamp, no ordering wobble: a regenerate with no schema change is an empty diff."""
    _, first = build()
    _, second = build()
    for name in first:
        assert schemagen.emit_class_source(first[name]) == schemagen.emit_class_source(second[name])
        assert schemagen.emit_header(first[name], first) == schemagen.emit_header(
            second[name], second
        )


def test_the_trimmed_baseline_keeps_only_what_the_generator_read():
    d, classes = build()
    enums = schemagen.collect_enums(d, classes)
    trimmed = schemagen.trimmed_dump(d, classes, enums)
    assert set(trimmed["classes"]) == set(classes)
    assert set(trimmed["enums"]) == {"MoveType_t"}
    assert trimmed["format"] == 1


def test_the_shipped_manifest_and_baseline_agree_with_each_other():
    """The committed pair is what the build compiles; a drift between them is a broken build."""
    root = Path(__file__).resolve().parents[2]
    baseline = json.loads((root / "schema" / "server.json").read_text(encoding="utf-8"))
    shipped = json.loads((root / "schema" / "manifest.json").read_text(encoding="utf-8"))
    classes = schemagen.build_classes(baseline, shipped)
    assert set(classes) == set(baseline["classes"])
