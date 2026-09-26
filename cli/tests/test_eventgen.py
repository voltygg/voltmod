from voltmod.framework.eventgen import collect_events, render_events

CORE = """
// comment before the block
"gameevents"
{
    "player_death"              // a player died
    {
        "userid"    "player_controller_and_pawn"   // user who died
        "attacker"  "player_controller"            // player who killed
        "dmg_health" "short"
        "hitgroup"  "byte"
        "pawn"      "player_pawn"
    }
    "bullet_impact"
    {
        "userid"    "short"
    }
}
"""

MOD = """
"gameevents"
{
    "player_team"
    {
        "userid"    "player_controller_and_pawn"
        "oldteam"   "byte"
        "local"     "1"
    }
    "player_death"
    {
        "userid"    "player_controller_and_pawn"
    }
}
"""


def events_by_name():
    return {event.key: event for event in collect_events([CORE, MOD])}


def test_a_later_file_replaces_an_earlier_event():
    death = events_by_name()["player_death"]
    assert [field.name for field in death.fields] == ["Slot"]


def test_player_keys_become_slots_and_other_keys_pascal_case():
    death = collect_events([CORE])[0]
    assert death.name == "PlayerDeath"
    assert [field.name for field in death.fields] == [
        "Slot",
        "AttackerSlot",
        "DmgHealth",
        "Hitgroup",
    ]
    assert death.fields[1].getter == 'e.GetPlayerSlot("attacker").Get()'
    assert death.comment == "a player died"
    assert death.fields[0].comment == "user who died"


def test_team_and_hitgroup_keys_carry_the_framework_enums():
    team = events_by_name()["player_team"]
    assert team.fields[1].name == "Oldteam"
    assert team.fields[1].cpp_type == "VoltMod::Team"
    hitgroup = collect_events([CORE])[0].fields[3]
    assert hitgroup.cpp_type == "VoltMod::HitGroup"


def test_unsupported_types_are_skipped_and_flags_are_not_fields():
    death = collect_events([CORE])[0]
    assert death.skipped == ["pawn: player_pawn"]
    team = events_by_name()["player_team"]
    assert [field.name for field in team.fields] == ["Slot", "Oldteam"]
    assert team.skipped == []


def test_hand_written_events_are_left_out_and_included():
    assert "bullet_impact" not in events_by_name()
    files = render_events(collect_events([CORE, MOD]))
    header = next(text for path, text in files.items() if path.suffix == ".hpp")
    assert "#include <VoltMod/Events/BulletImpact.hpp>" in header
    assert 'static constexpr std::string_view EventName = "player_team";' in header
