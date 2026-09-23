---
name: gamedata-update
description: Bring gamedata.jsonc and the schema baselines up to a new CS2 build - fetch the binaries, re-find broken signatures, re-check vtable indices and byte offsets by reverse engineering, regenerate the schema, and prove it on the local host. Use for "CS2 updated", "new cs2 update", "update gamedata", "update schema", "signature broke", "pattern not found", "vtable index moved", "reverse engineer <function>".
---

# Update gamedata and schema for a new CS2 build

`docs/sdk/gamedata.md` is the contract; this is the update-day procedure. Run everything from the
voltmod checkout. It has no `.env`, so pass the local server path (`C:/cs2-server`) explicitly.

## 1. Archive both builds

```bash
uv run voltmod doctor --server C:/cs2-server                 # server build vs gamedata stamp
uv run voltmod framework gamedata fetch --server C:/cs2-server
```

`fetch` archives the new build's binaries for both platforms under `~/.voltmod/cs2-builds/<build>/`,
and with `--server` files the server's current `resolved.<platform>.json` under the old build: the
old addresses to diff against. Only then update the local server:
`uv run voltmod serve --server C:/cs2-server --update`.

## 2. Patterns

```bash
uv run voltmod framework gamedata check --game-dir ~/.voltmod/cs2-builds/<new>/windows   # and linux
```

`--fix` repairs only a drifted struct offset. `script` entries resolve from VScript bindings at
load; a removed binding shows as `did not bind` in step 6.

Re-find the rest with snippets run through `binre.py`, in the framework environment:

```bash
uv run --with capstone --with numpy python .claude/skills/gamedata-update/scripts/binre.py snippet.py
```

In scope: `Binary.open(build, platform[, module])` with `find`, `disasm`, `function_at`,
`calls_in`, `strings_in`, `callers_of`, `functions_using`, `references_to`; plus `vtable`, `slots`,
`print_slots`, `make_pattern`, `drift`, `load_gamedata`, `load_resolved`. Most reliable first:

- **Shared callees**: a broken function usually calls the same helpers as an entry that still
  matches; take those call targets, then `callers_of(helper)`.
- **Strings**: `functions_using("TerminateRound")`, or a log line next to a global. Strings
  survive updates, prologues do not.
- **The other platform**: if one platform still matches, find a caller with a unique string on
  both and see which callee plays the same part.
- **Layout drift**: code order barely changes. `drift(pairs)` built from entries that still match
  predicts where an old address moved; read candidates within a few KB.
- **Semantics last**: check argument registers against the `Bindings.hpp` prototype, touched
  fields, the return. Twins can share a prologue (`AcceptInput` overloads, `CommitSuicide`
  overloads).

`make_pattern(binary, start, min_length)` wildcards branch targets, rip-relative and >= 0x100
displacements, and grows until unique; raise `min_length` to reach the tail that separates twins.
For a global, anchor on the instruction using it and keep `rel32At` small.

## 3. Vtable indices

Only a crash catches a shifted index, so check every entry. `print_slots(binary, class, first,
last)` on both platforms around the old index, then:

- Move the old `resolved` `code` RVA through `drift` and match it to a slot; a fit two slots on
  means virtuals were inserted before it.
- Pair slots across platforms by size, calls, strings and first instructions. Linux has two
  destructor slots to MSVC's one, and MSVC reverses overloads within a group, so the offset
  between platforms is not constant.
- MSVC folds identical stubs, so a slot on a shared `return true` is normal; verify it by its
  neighbours.
- `vtable(binary, class, subobject)` reaches a base's table (`CServerSideClient`'s
  `INetworkMessageProcessingPreFilter` is at 8 on Windows, 48 on Linux).

## 4. Byte offsets

Read the displacement of an instruction that uses the field on the new build. `m_szAddons` is the
`[this + X]` before `ReplyConnection`'s log line and the `mov rdx, [rcx + X]` in `CNetworkGameServer`
slot 26 (`GetAddonName`); a stale value fails safe (`m_szAddons offset ... is stale`). For client
fields, bracket them between known displacements (`m_SteamID` at 171 sits between 170 and 179).
Report which offsets were not checked offline.

## 5. Schema

Dumped per platform, only while a map runs. Windows: start the server with `-condebug`, wait for
`Schema: dumped game build <n>` in `game/csgo/addons/metamod/console.log`, then
`uv run voltmod framework schemagen --platform windows --server C:/cs2-server`. Linux: download
`csgo/addons/voltmod/schema/server.json` from a Linux server on the new build (`PanelApi`
`/files/download` returns a signed URL), then `framework schemagen --platform linux --dump <file>`.
A panel server is production: ask before restarting it (`poe deploy restart` in cs2-plugins).

## 6. Prove it

Set `build.server` and `build.verified` in `gamedata.jsonc` and run `check` on both platforms.
Install it into the local server, delete its `resolved.windows.json`, start it, and compare every
function, global and vtable `code` in the new record with what you found. No `did not bind` may
appear. Linux is proven only by a Linux host doing the same. Finish with `uv run poe lint` and the
`commit` skill; plugins need a rebuild because the layout stamp changed.

## Traps

- Git Bash rewrites `/game/...` arguments into Windows paths: call the panel API from PowerShell.
- Write snippet files with the file tool; the shell can mangle NUL bytes.
- A unique match can still be the wrong function: say how each entry was proven.
