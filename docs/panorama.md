# Panorama screens {#panorama_guide}

[TOC]

A screen is native Panorama XML and CSS - the rules in @ref custom_ui_guide apply -
written as [Jinja](https://jinja.palletsprojects.com/) templates so repetition (rows,
tabs, a bar, an icon set) is a loop instead of copy-paste. Rendering a screen also
derives its C++ binding, so a plugin's header cannot drift from the layout it names.

## Where a screen lives

One screen is `panorama/screens/<name>.xml.j2` plus `panorama/screens/<name>.css`,
both Jinja templates, under the owning plugin's `plugins/<name>/panorama` tree.
The plugin's directory name is its owner name.

The framework owns no screens. It ships `panorama/blocks/`, the macro library a
screen imports, which templates reach through the Jinja loader rather than by
ownership.

## A screen

```
{# namespace: ArenaUi::Hud #}
{% import "card.xml.j2" as cards %}
{% import "toast.xml.j2" as toasts %}
<root>
  <styles>
    <include src="file://{resources}/styles/custom_game/{{screen}}.css" />
  </styles>
  <Panel class="Layer" hittest="false">
    <Panel id="{{screen}}" class="Screen Hidden" hittest="false">
      {%- for index in range(3) %}
      {{ cards.card("card" ~ index, icon_set="weapons", bar=true, accent=true) }}
      {%- endfor %}
      {{ toasts.toast("toast") }}
    </Panel>
  </Panel>
</root>
```

```css
{% import "bar.css" as bar %}
{% import "icons.css" as icons %}
.Screen {
  width: 420px;
  flow-children: down;
}

.Screen.Hidden {
  visibility: collapse;
}

{% include "card.css" %}
{% include "toast.css" %}
{% include "accent.css" %}

.Accent.Accent--good { background-color: #4caf50; }
.Accent.Accent--bad { background-color: #f44336; }

{{ bar.fill_rules("Bar", 4) }}
{{ icons.show_rules("weapons") }}
```

The template context is `screen` (the file's own name) and `images` (every icon set
the owner ships, see [Images and icon sets](#panorama_guide_images)). Block macros
from the framework's `panorama/blocks/` are pulled in with `{% import %}`; their
default CSS is pulled in with `{% include %}`, once, in the screen's own stylesheet.
`{# namespace: X::Y #}` as the template's first line names the binding's C++
namespace; without it the namespace is `Screens::<Pascal>` (`hud.xml.j2` ->
`Screens::Hud`).

## Commands

```bash
voltmod build                            # renders panorama/screens/ before configuring
voltmod panorama render [OWNER...]       # write layouts, stylesheets, images and bindings
voltmod panorama check [OWNER...]        # read-only: the same checks the client applies silently
voltmod panorama compile [OWNER...]      # render, run resourcecompiler, install into your client (Windows)
voltmod panorama preview OWNER/SCREEN    # write an HTML approximation for a browser
voltmod panorama publish DIR [OWNER...]  # render, copy the rendered trees into an addon content directory
```

`OWNER` is a plugin name; omitted, every plugin that ships a screen renders. Nothing rendered is committed - a checkout renders
before it builds, and CI never needs the CS2 Workshop Tools.

`check` refuses what the client would otherwise reject silently on load: a
disallowed element type, a `Button` without an id or nested inside another
`Button`, a stylesheet included by anything but its source name, an `<Image src>`
that does not resolve to a real PNG under `images/custom_game/<set>/`, more than
900 interned names (see [The name budget](#panorama_guide_budget)), two owners
writing the same resource path, and a screen whose layout and stylesheet do not
derive a binding. It writes nothing, and it is part of `uv run poe lint`.

`compile` and `publish` render first, then compile with `resourcecompiler.exe` and
install into your own client, or copy the rendered tree into an addon content
directory for the Workshop Tools to build from - see [Publishing](#panorama_guide_publish).

## Preview {#panorama_guide_preview}

`voltmod panorama preview OWNER/SCREEN [--open]` writes a self-contained HTML file to
`build/panorama/preview/<screen>.html` - open it in any browser, no Workshop Tools,
compile, or client reconnect needed. A side panel drives the screen: a text box per
dialog variable, a dropdown per class family with a picker for the panel to write it
on, and a folded list of `Hidden` checkboxes, one per panel.

The page shell, its base stylesheet and the script wiring those controls up live in
`panorama/preview.html.in`, so the tool's own look is edited as HTML rather than as
strings in Python.

It approximates: Panorama CSS is translated property by property into ordinary web
CSS (`flow-children` to flex, `fill-parent-flow`/`fit-children` to flex sizing,
alignment to auto margins), so spacing, fonts, and anything CSS cannot express are
close but not exact. The gap worth knowing is a panel with no `flow-children` at all:
Panorama stacks its children, the preview lays them in a row, so a stacked column of
marks or a switch reads wrong here and right in game. It is a layout sketch, not the
client.

## Build tree outputs

```
build/panorama/<owner>/panorama/layout/custom_game/<name>.xml
build/panorama/<owner>/panorama/styles/custom_game/<name>.css
build/panorama/<owner>/panorama/images/custom_game/<set>/<icon>.png
build/panorama/<owner>/panorama/images/custom_game/<set>/<icon>.vtex
build/panorama/<owner>/include/Ui/<Pascal>.hpp
```

`cmake/VoltModPlugin.cmake` adds
`build/panorama/<owner>/include` as a private include directory for a plugin whose
own source tree has a `panorama/screens/` - so `#include <Ui/<Pascal>.hpp>`
resolves once `voltmod build` has rendered.

## What the header holds

`voltmod panorama render` reads a screen's rendered layout and stylesheet and emits
one constant for each name a plugin has to spell. It infers nothing beyond that:
how those constants become writers is the plugin's own code.

| In the screen | In the header |
| --- | --- |
| the outermost id | `Layout` and `RootId`; every other id must start with `<screen>_` |
| every other `id="..."` | a `std::string_view` named by what follows the screen prefix - `lab_card0_bar` becomes `Card0Bar` |
| `text="{s:var}"` | a `std::string_view` named `<Var>Var` |
| a block the template repeats - ids `<screen>_<stem><N>[_<suffix>]` and variables `<stem><N>[_<suffix>]` for N = 0..K-1 (K >= 2), alike in every copy | `struct <Stem>` with one `std::string_view` per member (`Id`, `<Suffix>`, `<Suffix>Var`, or `Var` for a bare variable) and `std::array<<Stem>, K> <Stem>s`; those names get no flat constant |
| a `Prefix--variant` class, in the layout or the stylesheet | a `PrefixClasses` array, plus `enum class Prefix` and `PrefixNames` unless every variant is a step number |

A family is found by a plain scan for `.Prefix--variant`, in layout order first and
then stylesheet order. The stylesheet half is not optional: an `Accent--*` or
`Step--*` family is declared only there, never in the layout.

`tests/Ui/Fixtures/Lab.hpp` is a real header, checked in and regenerated by
`cli/tests/test_bind.py` so the two never drift. It comes from a screen shaped like
the example above (two cards and a toast):

```cpp
inline constexpr std::string_view RootId = "lab";
inline constexpr std::string_view Toast = "lab_toast";
inline constexpr std::string_view ToastTitleVar = "toast_title";

struct Card { std::string_view Id, Accent, Icon, Bar, TitleVar, SubtitleVar, ValueVar; };
inline constexpr std::array<Card, 2> Cards{ Card{"lab_card0", "lab_card0_accent", ...}, Card{"lab_card1", ...} };

enum class Accent { Good, Bad };
inline constexpr std::array<std::string_view, 2> AccentNames{"good", "bad"};
inline constexpr std::array<std::string_view, 2> AccentClasses{"Accent--good", "Accent--bad"};
```

The plugin declares the writers it wants and builds one set per array entry with
@ref VoltMod::MakeWriters, which is what `tests/Ui/BoundScreenTests.cpp` and the `ui`
plugin's `Hud.cpp` both do:

```cpp
struct CardWriters
{
    VoltMod::TextVar Title, Subtitle, Value;
    VoltMod::ClassChoice Icon, Bar, Accent;
    VoltMod::ClassFlag Hidden;
};

constexpr CardWriters MakeCard(const LabUi::Card& card)
{
    return {.Title = {LabUi::RootId, card.TitleVar}, /* ... */ .Hidden = {card.Id, "Hidden"}};
}

constexpr auto Cards = VoltMod::MakeWriters(LabUi::Cards, MakeCard);
```

`TextVar`, `ClassFlag` and `ClassChoice` are `VoltMod/Ui/Writers.hpp` writers - see
@ref custom_ui_guide's "Screens and writers" section for how to call them.

## Block library

`panorama/blocks/` in the framework ships these macros. Import the `.xml.j2` with
`{% import %}` and pull in its default CSS with `{% include "<name>.css" %}`.

| Block | Signature | Draws |
| --- | --- | --- |
| `card` | `card(id, icon_set=none, bar=false, accent=false)` | a row: optional accent stripe and icon set, two lines of text, a value, an optional bar |
| `bar` | `bar(id)` | a meter panel; pair with `bar.css`'s `fill_rules(cls, steps)` macro for the `Step--0`..`Step--<steps>` width rules; `Hidden` on the bar takes it away |
| `icons` | `icons(id, set)` | one `<Image>` per PNG in the icon set, stacked; pair with `icons.css`'s `show_rules(set)` macro so each `Icon--<name>` class uncollapses its own image |
| `accent` | `accent(id)` | a colour stripe; the screen defines its own `.Accent--<name>` rules |
| `toast` | `toast(id)` | a notice that fades in when the driver puts class `Show` on it |
| `button` | `button(id, text, variant="")` | a labelled Button; `variant` adds a `Btn-<variant>` modifier |
| `dialog` | `dialog(id)`, called not imported | a centred panel with a crumb/title/subtitle header and a body slot |
| `listrow` | `listrow(id, marks=[], switch, hint, value, steppers, accent)` | one row of a list: accent stripe, lead column, two lines of text, a value, steppers |
| `tabs` | `tabs(id, count)` | a strip of hidden-by-default tabs, each reading `{s:<id><i>}` |
| `pager` | `pager(id)` | previous, a `{s:<id>}` label, next |

`dialog` takes its body through `{% call %}` rather than an argument:

```
{% call shell.dialog("panel") %}
  ...rows, pager, footer...
{% endcall %}
```

A block's CSS carries layout only - sizes, flow, alignment. Colour, radius and state belong to
the screen that includes it, which is what lets two screens share a row and still look different.
A static modifier uses one dash (`Btn-ghost`): `--` reads as a server-written class family.

## Images and icon sets {#panorama_guide_images}

Drop PNGs in an owner's `panorama/images/custom_game/<set>/`. Every set becomes an
entry in the `images` context (`images.weapons`, sorted file stems), so
`{% for name in images[set] %}` in a block can draw one `<Image>` per icon.
Rendering copies each PNG into the build tree and writes a matching `.vtex`
descriptor beside it - `resourcecompiler` compiles the descriptor, never the PNG
directly.

## The name budget {#panorama_guide_budget}

The client permanently interns every panel id, class name and dialog variable it
sees into a 1024-entry table. `voltmod panorama check` refuses a single screen
that would use more than 900 of them, which is what a runaway loop looks like.
Reuse ids and classes - a block's own macros already do this - rather than
generating a new name per instance.

## Publishing {#panorama_guide_publish}

`voltmod panorama publish DIR [OWNER...]` renders, then copies the rendered
`panorama/` trees into `DIR` with their paths intact - the content directory the
CS2 Workshop Tools build an addon from. See @ref workshop_guide for what a
workshop addon costs a connecting client and how to require one.
