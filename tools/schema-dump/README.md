# schema-dump

Dev-only plugin that writes the engine's schema to JSON. The file is the sole input to
`voltmod schemagen`, which generates the framework's schema accessor layer with offsets baked in.

Never deploy this to a live server: it is a developer tool, and a dump is a multi-megabyte write.

It lives here rather than in a consumer repo because its output format and `voltmod schemagen`
have to version together. It builds only when `VOLTMOD_BUILD_TOOLS=ON`, which this repo's CMake
presets set and Conan does not, so a package or editable consumer build never compiles it.

## Use

Install it into the local CS2 server and run the command from the **server console** (it is
console-only, since the plugin sets no permission policy):

```bash
uv run poe build            # VOLTMOD_BUILD_TOOLS is on in this repo's presets
uv run voltmod install schema-dump
uv run voltmod serve
```

```
schema_dump                 # -> addons/schema-dump/schema/server.json
schema_dump C:/tmp/out.json # explicit path
```

The reply names the path and the class, enum and field counts. Before a map has loaded the
schema system is not populated yet and the command answers `schema system not ready` — dump
after the first map load.

Update day is then one pass: `schema_dump`, `voltmod schemagen --dump <path>`, review the
`git diff` of the generated code, rebuild, redeploy.

## Output format

Prettified JSON, two-space indent, LF-terminated, keys ordered, versioned by `format`. One value
per line is deliberate: `git diff` on a regenerated baseline should show only the offsets that
actually moved. Only what the generator reads is written — no timestamp, alignment, abstract flag
or project name — so the committed baseline changes only when the schema itself does.

Both the global and the server type scope are walked, in that order, into one flat set of classes
and enums. The server scope alone is not self-contained: `MoveType_t`, `HitGroup_t`,
`CNetworkVarChainer` and the `CPlayerPawnComponent` base of the player-services classes are all
declared globally. A name defined in both scopes takes its server definition; the reply reports
how many names that affected, and on the CS2 build checked it was **0** - the two scopes are
disjoint, so the flat merge loses nothing.

```json
{
  "format": 1,
  "scopes": ["global", "server"],
  "classes": {
    "CCSPlayerPawn": {
      "size": 4992,
      "bases": [{"name": "CCSPlayerPawnBase", "offset": 0}],
      "chain_offset": -1,
      "fields": [
        {"name": "m_ArmorValue", "offset": 4820, "size": 4,
         "type": {"name": "int32", "category": "builtin"}}
      ]
    }
  },
  "enums": {
    "CSPlayerState": {"size": 4, "items": [{"name": "STATE_ACTIVE", "value": 0}]}
  }
}
```

`chain_offset` is the class's own `__m_pChainEntity` offset, found by walking single inheritance
up, or `-1`. It is what tells the generator whether a setter notifies the entity directly or
through the chainer.

A field's `type` object carries:

| Key | Meaning |
|---|---|
| `name` | the schema's own type string |
| `category` | `builtin`, `pointer`, `bitfield`, `fixed_array`, `atomic`, `declared_class`, `declared_enum`, `invalid` |
| `atomic` | the atomic sub-category (`plain`, `t`, `collection_of_t`, `tt`, `i`); only for `atomic` |
| `inner` | pointee, array element, or collection element type |
| `extent` | element count, for `fixed_array` |
| `declared` | `class` or `enum`, for the declared categories |

`atomic` is recorded separately because every `CUtlVector` is `SCHEMA_TYPE_ATOMIC`: the category
alone cannot tell a collection from a plain atomic, and a collection hangs its element type off a
different member than a pointer or a fixed array does.

Absent keys are omitted rather than written as null, so a builtin field's type object is just
`name` and `category`.
