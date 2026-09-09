# Panorama screens {#panorama_guide}

[TOC]

A screen is native Panorama XML and CSS - the rules in @ref custom_ui_guide apply -
written as [Jinja](https://jinja.palletsprojects.com/) templates so repetition (rows,
tabs, a bar, an icon set) is a loop instead of copy-paste. Rendering a screen also
derives its C++ binding, so a plugin's header cannot drift from the layout it names.

## Where a screen lives

One screen is `panorama/screens/<name>.xml.j2` plus `panorama/screens/<name>.css`,
both Jinja templates, under an owner's own `panorama/` tree:

- the framework's own, at the repository root of `vendor/voltmod` (owner `voltmod`)
- each plugin's, at `plugins/<name>/panorama` (owner `<name>`)

The project's own root `panorama/` is not an owner: it holds only `panorama/skin/`,
plain CSS appended to another owner's screens (see [The skin file](#panorama_guide_skin)).

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

{{ bar.fill_rules("Bar-fill", 4) }}
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

`OWNER` is a plugin name, or `voltmod` for the framework's own screens; omitted,
every owner found renders. Nothing rendered is committed - a checkout renders
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
compile, or client reconnect needed. A side panel lists every dialog variable as a
text box, every flag as a checkbox, and every class family as a dropdown, wired to
the same ids the derived C++ binding uses.

It approximates: Panorama CSS is translated property by property into ordinary web
CSS (`flow-children` to flex, `fill-parent-flow`/`fit-children` to flex sizing,
alignment to auto margins), so spacing, fonts, and anything CSS cannot express are
close but not exact. It is a layout sketch, not the client.

## Build tree outputs

```
build/panorama/<owner>/panorama/layout/custom_game/<name>.xml
build/panorama/<owner>/panorama/styles/custom_game/<name>.css
build/panorama/<owner>/panorama/images/custom_game/<set>/<icon>.png
build/panorama/<owner>/panorama/images/custom_game/<set>/<icon>.vtex
build/panorama/<owner>/include/Ui/<Pascal>.hpp   (plugin owners only)
```

The framework itself has no plugin of its own to bind a header into, so `voltmod`
never gets an `include/` tree. `cmake/VoltModPlugin.cmake` adds
`build/panorama/<owner>/include` as a private include directory for a plugin whose
own source tree has a `panorama/screens/` - so `#include <Ui/<Pascal>.hpp>`
resolves once `voltmod build` has rendered.

## Binding rules

`voltmod panorama render` derives a screen's C++ binding from its rendered layout
and stylesheet - see the module docstring of `cli/voltmod/builder/panorama/bind.py`
for the exact grammar. In short:

| In the screen | In the header |
| --- | --- |
| the outermost id | the screen name; every other id must start with `<screen>_` |
| a path segment with trailing digits (`card0`) | an array index (`Card[0]`) |
| `<Button id="...">` | `std::string_view` id |
| `text="{s:var}"` | `VoltMod::Text` naming the dialog variable |
| a class the stylesheet only ever pairs with the panel's own first class | a state: a plain class is a `VoltMod::Flag`; a `Prefix--variant` class joins that panel to a `Prefix` family |
| every direct child carrying one class of a `Prefix--variant` family | also a `Prefix` family (how an icon set names its icons) |
| a family with more than one writer on the screen | one `VoltMod::OneOf<N>`, a `PrefixClasses` array, and - unless every variant is a step number - `enum class Prefix` plus `PrefixNames` |
| a panel with exactly one writer and no deeper path | the writer takes the panel's own leaf name instead of a nested member |
| a path with children | a `<Path>Panel` struct; an array entry is a `<Path>Item` |

`tests/Ui/Fixtures/Lab.hpp` is a real derived header, checked in and regenerated by
`cli/tests/test_bind.py` so the two never drift. It comes from a screen shaped like
the example above (two cards and a toast):

```cpp
struct CardItem
{
    VoltMod::Flag Hidden;
    VoltMod::OneOf<2> Accent;
    VoltMod::OneOf<2> Icon;
    VoltMod::Text Title;
    VoltMod::Text Subtitle;
    VoltMod::Text Value;
    VoltMod::OneOf<5> Bar;
};

inline constexpr std::array<CardItem, 2> Card{{ /* one entry per card */ }};

struct ToastPanel
{
    VoltMod::Flag Show;
    VoltMod::Text Title;
    VoltMod::Text Description;
    VoltMod::OneOf<2> Accent;
};

inline constexpr ToastPanel Toast{ /* ... */ };
```

`Text`, `Flag` and `OneOf<N>` are `VoltMod::Ui::Widgets.hpp` writers - see
@ref custom_ui_guide's "Screens and writers" section for how to call them.

## Block library

`panorama/blocks/` in the framework ships these macros. Import the `.xml.j2` with
`{% import %}` and pull in its default CSS with `{% include "<name>.css" %}`.

| Block | Signature | Draws |
| --- | --- | --- |
| `card` | `card(id, icon_set=none, bar=false, accent=false)` | a row: optional accent stripe and icon set, two lines of text, a value, an optional bar |
| `bar` | `bar(id)` | a meter panel; pair with `bar.css`'s `fill_rules(cls, steps)` macro for the `Step--0`..`Step--<steps>` width rules |
| `icons` | `icons(id, set)` | one `<Image>` per PNG in the icon set, stacked; pair with `icons.css`'s `show_rules(set)` macro so each `Icon--<name>` class uncollapses its own image |
| `accent` | `accent(id)` | a colour stripe; the screen defines its own `.Accent--<name>` rules |
| `toast` | `toast(id)` | a notice that fades in when the driver puts class `Show` on it |

## The skin file {#panorama_guide_skin}

`panorama/skin/<screen>.css` at the project root, if it exists, is appended as
plain CSS - not a Jinja template - after the screen's own rendered stylesheet. It
is the one place to restyle a screen you do not own without touching its
template, and it is never generated or overwritten.

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
