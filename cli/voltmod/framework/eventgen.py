"""Game event structs, generated from the `.gameevents` files the game ships in its VPKs."""

import re
from dataclasses import dataclass
from pathlib import Path

import vpk

from voltmod.bundled import load_template
from voltmod.errors import VoltmodError
from voltmod.framework.paths import INCLUDE_ROOT
from voltmod.panorama.layout import pascal_case

EVENTS_HEADER = INCLUDE_ROOT / "Events/EventTypes.hpp"
EVENTS_SOURCE = Path("src/Events/EventTypes.cpp")

# Later files win when two define the same event.
EVENT_FILES = (
    ("game/core/pak01_dir.vpk", "resource/core.gameevents"),
    ("game/csgo/pak01_dir.vpk", "resource/game.gameevents"),
    ("game/csgo/pak01_dir.vpk", "resource/mod.gameevents"),
)

# Keys that set how the event is sent, not a field.
FLAG_KEYS = {"local", "reliable"}

PLAYER_TYPES = {"player_controller", "player_controller_and_pawn"}

# Game type -> (C++ type, IGameEvent getter, default value).
VALUE_TYPES = {
    "string": ("std::string", 'e.GetString("{key}", "")', ""),
    "bool": ("bool", 'e.GetBool("{key}")', "false"),
    "byte": ("int", 'e.GetInt("{key}")', "0"),
    "short": ("int", 'e.GetInt("{key}")', "0"),
    "long": ("int", 'e.GetInt("{key}")', "0"),
    "int": ("int", 'e.GetInt("{key}")', "0"),
    "float": ("float", 'e.GetFloat("{key}")', "0.0f"),
    "uint64": ("uint64_t", 'e.GetUint64("{key}")', "0"),
}

TEAM = ("VoltMod::Team", 'static_cast<VoltMod::Team>(e.GetInt("{key}"))', "VoltMod::Team::None")

# Keys whose number is one of the framework's enums, whatever the event.
TYPED_KEYS = {
    "team": TEAM,
    "oldteam": TEAM,
    # A missing hitgroup reads 0, which is HitGroup::Generic.
    "hitgroup": (
        "VoltMod::HitGroup",
        'static_cast<VoltMod::HitGroup>(e.GetInt("{key}"))',
        "VoltMod::HitGroup::Generic",
    ),
}

# Events written by hand in Events/<Name>.hpp, which the generated header includes.
HAND_WRITTEN = {"bullet_impact": "BulletImpact"}


@dataclass(frozen=True, slots=True)
class EventField:
    name: str  # the C++ member
    cpp_type: str
    getter: str
    default: str
    comment: str


@dataclass(frozen=True, slots=True)
class GameEvent:
    key: str  # the engine's event name
    name: str  # the C++ struct
    comment: str
    fields: list[EventField]
    skipped: list[str]  # `key: reason` lines


def parse_keyvalues(text: str) -> list[tuple[str, object, str]]:
    """A KeyValues file's top block as (key, value or nested list, trailing comment) triples."""
    pattern = re.compile(r'//([^\n]*)|"([^"]*)"|([{}])|([^\s{}"]+)|(\n)')
    tokens: list[tuple[str, str]] = []  # (kind, text): "word", "{", "}", "comment", "newline"
    for match in pattern.finditer(text):
        comment, quoted, brace, bare, newline = match.groups()
        if comment is not None:
            tokens.append(("comment", comment.strip()))
        elif newline is not None:
            tokens.append(("newline", ""))
        elif brace is not None:
            tokens.append((brace, brace))
        else:
            tokens.append(("word", quoted if quoted is not None else bare))

    position = 0

    def trailing_comment() -> str:
        # A comment on the same line as the pair it follows describes that pair.
        nonlocal position
        if position < len(tokens) and tokens[position][0] == "comment":
            position += 1
            return tokens[position - 1][1]
        return ""

    def block() -> list[tuple[str, object, str]]:
        nonlocal position
        entries: list[tuple[str, object, str]] = []
        while position < len(tokens):
            kind, key = tokens[position]
            position += 1
            if kind == "}":
                return entries
            if kind != "word":
                continue
            comment = trailing_comment()
            while tokens[position][0] in ("newline", "comment"):
                position += 1
            kind, value = tokens[position]
            position += 1
            if kind == "{":
                entries.append((key, block(), comment))
            else:
                entries.append((key, value, comment or trailing_comment()))
        return entries

    return block()


def event_model(key: str, body: list[tuple[str, object, str]], comment: str) -> GameEvent:
    fields: list[EventField] = []
    skipped: list[str] = []
    for field_key, game_type, field_comment in body:
        if field_key in FLAG_KEYS or not isinstance(game_type, str):
            continue
        if game_type in PLAYER_TYPES:
            name = "Slot" if field_key == "userid" else pascal_case(field_key) + "Slot"
            field = EventField(
                name, "int", f'e.GetPlayerSlot("{field_key}").Get()', "-1", field_comment
            )
        elif spec := TYPED_KEYS.get(field_key) or VALUE_TYPES.get(game_type):
            cpp_type, getter, default = spec
            field = EventField(
                pascal_case(field_key),
                cpp_type,
                getter.format(key=field_key),
                default,
                field_comment,
            )
        else:
            skipped.append(f"{field_key}: {game_type}")
            continue
        taken = {"EventName", "From", pascal_case(key), *(other.name for other in fields)}
        if field.name in taken:
            skipped.append(f"{field_key}: its name {field.name} is taken")
            continue
        fields.append(field)
    return GameEvent(key, pascal_case(key), comment, fields, skipped)


def read_events(server: Path) -> list[GameEvent]:
    """Every event the server's game files define, minus the hand-written ones."""
    archives = {}
    texts = []
    for archive, inner in EVENT_FILES:
        if archive not in archives:
            path = server / archive
            if not path.is_file():
                raise VoltmodError(f"{path} is missing; point CS2_SERVER_PATH at a CS2 server")
            archives[archive] = vpk.open(str(path))
        texts.append(archives[archive].get_file(inner).read().decode("utf-8", errors="replace"))
    return collect_events(texts)


def collect_events(texts: list[str]) -> list[GameEvent]:
    """The events in `texts`, sorted by name; a later file wins over an earlier one."""
    events: dict[str, tuple[list[tuple[str, object, str]], str]] = {}
    for text in texts:
        top = parse_keyvalues(text)
        if not top or not isinstance(top[0][1], list):
            raise VoltmodError("a .gameevents file has no events block")
        for key, body, comment in top[0][1]:
            if isinstance(body, list):
                events[key] = (body, comment)
    return [
        event_model(key, body, comment)
        for key, (body, comment) in sorted(events.items())
        if key not in HAND_WRITTEN
    ]


def render_events(events: list[GameEvent]) -> dict[Path, str]:
    context = {"events": events, "hand_written": sorted(HAND_WRITTEN.values())}
    return {
        EVENTS_HEADER: load_template("eventgen/EventTypes.hpp.j2").render(**context),
        EVENTS_SOURCE: load_template("eventgen/EventTypes.cpp.j2").render(**context),
    }
