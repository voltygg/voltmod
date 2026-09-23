---
name: gamedata-update
description: Bring gamedata.jsonc and the schema baselines up to a new CS2 build - fetch the binaries, re-find broken signatures, re-check vtable indices and byte offsets by reverse engineering, regenerate the schema, and prove it on the local host. Use for "CS2 updated", "new cs2 update", "update gamedata", "update schema", "signature broke", "pattern not found", "vtable index moved", "reverse engineer <function>".
---

# Update gamedata and schema for a new CS2 build

`docs/sdk/gamedata.md` is the contract (sections, what the host checks). This skill is the
update-day procedure. Work in the framework checkout; `uv run` there.

## 1. Get both builds side by side

```bash
uv run voltmod doctor --server C:/cs2-server        # is the server current? gamedata stamp vs server
uv run voltmod framework gamedata fetch              # archive this build's binaries, both platforms
```

The archive (`~/.voltmod/cs2-builds/<build>/<platform>/`, laid out like a server) is the only
source of an old build; see `docs/sdk/gamedata.md`. Run `fetch` before updating the local server:
it files the server's `resolved.<platform>.json` under the old build it names, which gives the
old addresses to diff against.

Update the local server (`voltmod serve --update`, which also restores the Metamod line
the update strips from `gameinfo.gi`).

## 2. Patterns

```bash
uv run voltmod framework gamedata check --game-dir ~/.voltmod/cs2-builds/<new>/windows
uv run voltmod framework gamedata check --game-dir ~/.voltmod/cs2-builds/<new>/linux
uv run voltmod framework gamedata check --fix --game-dir ...   # only fixes a drifted struct offset
```

`script` entries (`CBaseEntity::EmitSoundParams`, `CCSPlayerController::ChangeTeam`) have nothing
to check offline: the host reads them from the VScript bindings at load, and a binding Valve
removed shows up as a `did not bind` line in step 6.

Re-find everything `check --fix` cannot with `scripts/binre.py`, which runs a snippet with the
helpers of `scripts/binary.py` (disassembly, references, callers) and `scripts/analysis.py`
(slots, patterns, drift) in scope. Both build on the CLI's `voltmod.framework.binaries`, so run
it in the framework's environment:
`uv run --with capstone --with numpy python .claude/skills/gamedata-update/scripts/binre.py snippet.py`.
`Binary.open(build, platform)` takes a build number from the archive, and `binary.image` is the
CLI's `Image`. What worked, most reliable first:

- **Shared callees.** A broken function usually calls the same helpers as an entry that still
  matches (the HUD `ForPlayer` setters call the same name lookups as `SetHasClass`). Take the
  working one's call targets, then `callers_of(helper)`.
- **Strings.** `functions_using("TerminateRound")`, or the log line next to a global
  (`"gameeventmanager->Init()"`). Strings survive updates; prologues do not.
- **The other platform.** When one platform still matches, find a caller with a unique string on
  both, and read which of its callees plays the same part (`SetPosition` -> `AddEntityIOEvent`).
- **Layout drift.** Code order barely changes between builds. From the old `resolved` record and
  entries that still match, `drift(pairs)` predicts where an old address moved; candidates
  within a few KB of the prediction are worth reading. Relative distances between two known
  functions survive almost exactly.
- **Semantics last.** Read the candidate: argument registers against the `Bindings.hpp`
  prototype, fields it touches, what it returns. A match is not proof: two twins can share a
  prologue (`AcceptInput` string vs symbol overload, the two `CommitSuicide` overloads).

Write the new pattern with `make_pattern(binary, start, min_length)`: it wildcards branch
targets, rip-relative and >= 0x100 struct displacements, and grows until unique. Extend past the
prologue into instructions that say what the function is, and raise `min_length` to reach the
tail that tells twins apart. For a global, anchor on the instruction using it and keep
`rel32At` small rather than counting from a function start.

## 3. Vtable indices

Nothing catches a shifted index but a crash, so check every entry, not only broken ones.
`print_slots(binary, class, first, last)` on both platforms around the old index, then:

- Compare the old `resolved` `code` RVA, moved by `drift`, with each slot; a big gap at the old
  index and a fit two slots on means virtuals were inserted before it.
- Pair slots across platforms by size, call count, strings and first instructions. Linux runs
  ahead of Windows by the destructor (two Itanium slots vs one MSVC), and MSVC reverses
  overloads within a group, so the offset between platforms is not constant along a table.
- `slots()` stops at the table end; an entry near the end cannot have moved past it.
- Identical `return true` stubs are folded by MSVC, so a handler slot pointing at a shared stub
  is normal (`ProcessRespondCvarValue`); verify it by its neighbours instead.
- A slot's code must match the `code` the host records in `resolved.<platform>.json`.
- `vtable(binary, class, subobject)` takes a base's offset for its own table (the
  `INetworkMessageProcessingPreFilter` table of `CServerSideClient` is at 8 on Windows, 48 on Linux).

## 4. Byte offsets

Find an instruction that reads the field and read its displacement on the new build: the
addons string is `[this + X]` in `ReplyConnection` right before its log line, and also the
`mov rdx, [rcx + X]` in `CNetworkGameServer`'s `GetAddonName` slot (26). The host compares that
offset with `GetAddonName` before every write and logs `m_szAddons offset ... is stale` instead of
writing, so a missed update fails safe. For client fields, count unaligned displacements across
the class's virtuals (`m_SteamID` at 171 sits between 170 and 179). Say which offsets were not
checked offline.

## 5. Schema

The dump exists only while a map runs, per platform. Windows: start the local server with a map
(`voltmod serve`), wait for `Schema: dumped game build <n>` in
`game/csgo/addons/metamod/console.log` (launch with `-condebug`), then
`uv run voltmod framework schemagen --platform windows --server C:/cs2-server`. Linux: the dump must
come from a Linux server on the new build; pull `csgo/addons/voltmod/schema/server.json` from a
panel server with `PanelApi` (`/files/download` returns a signed URL), then
`framework schemagen --platform linux --dump <file>`. A panel server lags until its host updates; ask the
user before restarting one (`poe deploy restart`), it is production.

## 6. Prove it

Set `build.server` and `build.verified`, run `gamedata check` on both platforms, then install
`gamedata.jsonc` into the local server, delete its `resolved.windows.json`, start it, and
compare every function, global and vtable `code` in the new record with what you found. The
log must show no `did not bind` line. Linux is only proven by a Linux host doing the same.
Finish with `uv run poe lint`, then the `commit` skill; plugins need a rebuild because the
layout stamp changed.

## Traps

- Git Bash rewrites `/game/...` arguments into Windows paths: call the panel API from PowerShell.
- Keep NUL bytes out of snippet files written through the shell; write them with the file tool.
- A pattern that matches once can still be the wrong function: say how each entry was proven.
