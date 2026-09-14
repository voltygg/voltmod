# Panorama screens {#panorama_guide}

[TOC]

A screen is native Panorama XML and CSS - the rules in @ref custom_ui_guide apply -
written as [Jinja](https://jinja.palletsprojects.com/) templates so repetition (rows,
tabs, an icon set) is a loop instead of copy-paste. Rendering a screen also emits a
C++ header naming its ids, so a plugin's code cannot drift from the layout it writes.

## Where a screen lives

One screen is `panorama/screens/<name>.xml.j2` plus `panorama/screens/<name>.css.j2`,
both Jinja templates, under the owning plugin's `plugins/<name>/panorama` tree.
The plugin's directory name is its owner name.

The framework owns no screens. It ships `panorama/blocks/`, the macro library a
screen imports, which templates reach through the Jinja loader rather than by
ownership.

## A screen

```text
{# namespace: ArenaLayout #}
{% import "button.xml.j2" as controls %}
{% import "icons.xml.j2" as icons %}
{% import "listrow.xml.j2" as list %}
<root>
  <styles>
    <include src="file://{resources}/styles/custom_game/{{screen}}.css" />
  </styles>
  <Panel class="Layer" hittest="false">
    <Panel id="{{screen}}" class="Screen Hidden" hittest="false">
      {{ icons.icons("icon", "weapons") }}
      {%- for index in range(3) %}
      {{ list.listrow("row" ~ index, hint=true, chevron=true) }}
      {%- endfor %}
      {{ controls.button("close", "{s:close}") }}
    </Panel>
  </Panel>
</root>
```

```css
{% import "icons.css.j2" as icons %}
.Screen {
  width: 420px;
  flow-children: down;
}

.Screen.Hidden {
  visibility: collapse;
}

{% include "listrow.css.j2" %}
{% include "button.css.j2" %}

{{ icons.show_rules("weapons") }}
```

The template context is `screen` (the file's own name) and `images` (every icon set
the owner ships, see [Images and icon sets](#panorama_guide_images)). Block macros
from the framework's `panorama/blocks/` are pulled in with `{% import %}`; their
default CSS is pulled in with `{% include %}`, once, in the screen's own stylesheet.
`{# namespace: X #}` as the template's first line names the header's C++ namespace;
without it the namespace is `Screens::<Pascal>` (`hud.xml.j2` -> `Screens::Hud`).

## Commands

```bash
voltmod build                            # renders panorama/screens/ before configuring
voltmod panorama render [OWNER...]       # write layouts, stylesheets, images and headers
voltmod panorama check [OWNER...]        # read-only: the same checks the client applies silently
voltmod panorama compile [OWNER...]      # render, run resourcecompiler, install into your client (Windows)
```

`OWNER` is a plugin name; omitted, every plugin that ships a screen renders. Nothing
rendered is committed - a checkout renders before it builds, and CI never needs the
CS2 Workshop Tools.

`check` refuses what the client would otherwise reject silently on load: a
disallowed element type, a `Button` without an id or nested inside another
`Button`, a stylesheet included by anything but its source name, an `<Image src>`
that is neither a game icon nor a real PNG under `images/custom_game/<set>/`, too
many interned names (see [The name budget](#panorama_guide_budget)), two owners
writing the same resource path, and a screen whose names cannot be spelled in C++.
It writes nothing, and it is part of `uv run poe lint`.

`compile` renders first, then compiles with `resourcecompiler.exe` and installs
into your own client. `--no-deploy` leaves the client alone, which is how a
workshop addon is built - see [Publishing](#panorama_guide_publish).

## Build tree outputs

```text
build/panorama/<owner>/panorama/layout/custom_game/<name>.xml
build/panorama/<owner>/panorama/styles/custom_game/<name>.css
build/panorama/<owner>/panorama/images/custom_game/<set>/<icon>.png
build/panorama/<owner>/panorama/images/custom_game/<set>/<icon>.vtex
build/panorama/<owner>/include/Ui/<Pascal>.hpp
```

`cmake/VoltModPlugin.cmake` adds `build/panorama/<owner>/include` as a private
include directory for a plugin whose own source tree has a `panorama/screens/` - so
`#include <Ui/<Pascal>.hpp>` resolves once `voltmod build` has rendered.

## What the header holds

`voltmod panorama render` reads a screen's rendered layout and stylesheet and emits
one constant for each name a plugin has to spell. A plugin writes those names
through @ref VoltMod::Screen.

| In the screen | In the header |
| --- | --- |
| the outermost id | `Layout` and `RootId`; every other id must start with `<screen>_` |
| every other `id="..."` | a `std::string_view` named by what follows the screen prefix - `lab_close` becomes `Close` |
| `text="{s:var}"` | a `std::string_view` named `<Var>Var` |
| a block the template repeats - ids `<screen>_<stem><N>[_<suffix>]` and variables `<stem><N>[_<suffix>]` for N = 0..K-1 (K >= 2), alike in every copy | `struct <Stem>` with one `std::string_view` per member (`Id`, `<Suffix>`, `<Suffix>Var`, or `Var` for a bare variable) and `std::array<<Stem>, K> <Stem>s`; those names get no flat constant |
| a `Prefix--variant` class, in the layout or the stylesheet | a `PrefixClasses` array, and a `PrefixNames` array of the variants alone in the same order |

A family is found by a plain scan for `.Prefix--variant`, in layout order first and
then stylesheet order. The stylesheet half matters: a family only selectors use is
declared nowhere in the layout.

`tests/Ui/Fixtures/Lab.hpp` is a real header, checked in and regenerated by
`cli/tests/test_panorama_layout.py` so the two never drift. It comes from a screen
shaped like the example above, with two rows:

```cpp
inline constexpr std::string_view RootId = "lab";
inline constexpr std::string_view Close = "lab_close";
inline constexpr std::string_view CloseVar = "close";

struct Row { std::string_view Id, Button, Decrease, Increase, LabelVar, HintVar, ValueVar; };
inline constexpr std::array<Row, 2> Rows{ Row{"lab_row0", "lab_row0_button", ...}, Row{"lab_row1", ...} };

inline constexpr std::array<std::string_view, 2> IconClasses{"Icon--ak47", "Icon--awp"};
inline constexpr std::array<std::string_view, 2> IconNames{"ak47", "awp"};
```

Writing one row of it:

```cpp
const LabUi::Row& row = LabUi::Rows[index];
screen.SetHidden(slot, row.Id, false);
screen.SetText(slot, row.LabelVar, "Kick");
```

## Block library

`panorama/blocks/` in the framework ships these macros. Import the `.xml.j2` with
`{% import %}` and pull in its default CSS with `{% include "<name>.css.j2" %}`.

| Block | Signature | Draws |
| --- | --- | --- |
| `card` | `card(id, icon_set=none, bar=false)` | a HUD row: an optional icon set, two lines of text, a value, an optional bar along the bottom; starts `Hidden`, and the icon takes room only while `HasIcon` is on the card |
| `bar` | `bar(id)` | a meter; pair with `bar.css.j2`'s `fill_rules(cls, steps)` macro for the `Step--0`..`Step--<steps>` width rules; `Hidden` on the bar takes it away |
| `toast` | `toast(id)` | a notice that fades in while class `Show` is on it |
| `icons` | `icons(id, set)` | one `<Image>` per PNG in the icon set, or per `(name, icon)` pair of client icons, stacked; pair with `icons.css.j2`'s `show_rules(set)` macro so each `Icon--<name>` class uncollapses its own image |
| `button` | `button(id, text, variant="")` | a labelled Button; `variant` adds a `Button-<variant>` modifier |
| `dialog` | `dialog(id)`, called not imported | a centred panel with a breadcrumb/title/subtitle header and a body slot |
| `listrow` | `listrow(id, switch, hint, value, steppers, chevron)` | one row of a list: two lines of text, a value, a collapsed switch and chevron the screen shows per row class, and steppers; its ids end `_button`, `_decrease` and `_increase` |
| `tabs` | `tabs(id, count)` | a strip of hidden-by-default tabs, each reading `{s:<id><i>}` |
| `pager` | `pager(id)` | previous (`_previous`), a `{s:<id>}` label, next (`_next`) |

`dialog` takes its body through `{% call %}` rather than an argument:

```text
{% call shell.dialog("panel") %}
  ...rows, pager, footer...
{% endcall %}
```

A block's CSS carries layout only - sizes, flow, alignment. Colour, radius and state
belong to the screen that includes it, which is what lets two screens share a row and
still look different. A static modifier uses one dash (`Button-ghost`): `--` reads as
a server-written class family.

## Images and icon sets {#panorama_guide_images}

Drop PNGs in an owner's `panorama/images/custom_game/<set>/`. Every set becomes an
entry in the `images` context (`images.weapons`, sorted file stems), so
`{% for name in images[set] %}` in a block can draw one `<Image>` per icon.
Rendering copies each PNG into the build tree and writes a matching `.vtex`
descriptor beside it - `resourcecompiler` compiles the descriptor, never the PNG
directly.

The client's own icons need no files at all. Point an `<Image>` at
`s2r://panorama/images/icons/ui/<name>.vsvg` (`settings`, `player`, `message`, ...) and
tint it with `wash-color`; they are vectors, so give them `textureheight="32"` to rasterise
sharply at menu sizes. The `icons` block does all three when `set` is a list of
`(name, icon)` pairs.

## The name budget {#panorama_guide_budget}

The client permanently interns every element id, class name and variable it sees
into a 1024-entry table, and that table is shared by every screen it loads.
`voltmod panorama check` counts the names of all the screens it builds and
refuses the set once it passes 1024, naming the biggest screens first. It also
refuses any single screen past 400 names on its own, which is what a runaway
loop looks like rather than a design. Reuse ids and classes - a block's own
macros already do this - rather than generating a new name per instance.

## Publishing {#panorama_guide_publish}

Other players get the screens from a workshop addon:

1. `voltmod panorama compile [OWNER...] --addon NAME --no-deploy` compiles into
   `game/csgo_addons/NAME/` without touching your client.
2. Open NAME in the CS2 Workshop Tools, then the Workshop Manager, and submit it as
   Public or Unlisted. A private item does not download for anyone else.
3. Require the published id from the plugin.

Delete what an earlier `compile` installed under your client's
`game/csgo/panorama/*/custom_game/` before testing the download, or the client
keeps using those files. See @ref workshop_guide for what an addon costs a
connecting client.
