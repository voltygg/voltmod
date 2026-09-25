# Panorama screens {#panorama_guide}

[TOC]

A screen is native Panorama XML and CSS written as [Jinja](https://jinja.palletsprojects.com/)
templates, so repetition (rows, tabs, an icon set) is a loop instead of copy-paste. Rendering also
emits a C++ header naming every id, so plugin code cannot drift from the layout it writes. Driving
a rendered screen from C++ is @ref custom_ui_guide.

## Where a screen lives

One screen is `panorama/screens/<name>.xml.j2` plus `panorama/screens/<name>.css.j2` under the
owning plugin's `plugins/<name>/panorama` tree. The plugin's directory name is its owner name. The
framework owns no screens; it ships `panorama/blocks/`, the macro library a screen imports through
the Jinja loader.

```text
{% extends "screen.xml.j2" %}
{% import "button.xml.j2" as controls %}
{% import "icons.xml.j2" as icons %}
{% block content %}
      {{ icons.icons("icon", png_icons("weapons")) }}
      {%- for index in range(3) %}
      <Button id="{{screen}}_row{{index}}" class="row">
        <Label class="row__label" text="{s:row{{index}}_label}" hittest="false" />
      </Button>
      {%- endfor %}
      {{ controls.button("close", "{s:close}") }}
{% endblock %}
```

```css
{% import "icons.css.j2" as icons %}
{% include "screen.css.j2" %}
{% include "button.css.j2" %}

.row {
  width: 420px;
  height: 40px;
}

{{ icons.show_rules(png_icons("weapons")) }}
```

Every screen extends `screen.xml.j2`, which includes the stylesheet and makes the root panel with
the screen name as its id. The root starts `hidden`; `{% set starts_hidden = false %}` shows it at
once, and `{% set hittest = false %}` lets clicks through it. Every stylesheet includes
`screen.css.j2`, which sizes the root and defines `hidden`.

The template context is `screen` (the file's own name) plus the icon helpers under
[Images and icon sets](#panorama_guide_images). Block macros come in with `{% import %}`; their
default CSS comes in with `{% include %}`, once, in the screen's own stylesheet. The header's C++
namespace is `<Pascal>Layout` (`stronghold_hud.xml.j2` gives `StrongholdHudLayout`).

Templates several plugins share go in one plugin's `panorama/templates/`, under a folder named for
the kit (`templates/brand/...`). Import one through its explicit `@<plugin>/` namespace:

```jinja
{% import "@brand-kit/brand/logo.xml.j2" as brand %}
```

Here `@brand-kit/brand/logo.xml.j2` resolves only to
`plugins/brand-kit/panorama/templates/brand/logo.xml.j2`. Bare names resolve against the screen's
own `screens/` tree and then the framework block library; plugin template directories are never
merged into that search path.

## Theme tokens

`@define name: value;` declares a token; use it as a bare `name`. A define reaches only its own
stylesheet, so `{% include %}` a shared partial at the top of each screen's CSS. Tokens do not
compose (`rgba(accent, 0.6)` fails), so give each alpha variant its own.

```css
@define accent: #e1273c;
@define accent-soft: rgba(225, 39, 60, 0.16);

.tab.tab--selected { background-color: accent; }
```

## What the client accepts

The client validates markup and reports failures only in the client console, so
`voltmod panorama check` refuses the same things first:

- Only `Panel`, `Label`, `Image` and `Button` (plus `<root>`, `<styles>` and `<include>`).
- Only the attributes `id`, `class`, `hittest`, `text` and `src`. Anything else, even a
  valid Panorama one such as `scaling` or `textureheight`, fails the client's custom HUD
  validation and the whole layout is dropped.
- Every `Button` needs an `id`, and a `Button` may not sit inside another `Button` - the inner
  press is lost. Make them siblings and size them side by side.
- Every id is unique and starts with `<screen>_`, except the outermost one, which is the screen
  name itself.
- An `<Image src>` is either a game icon or a real PNG under `images/<set>/`.

For reliable clicks, every panel on the path to a `Button` needs a resolved size (`width: 100%`, a
fixed value, or `fill-parent-flow`); a container left to size itself around its children renders
the buttons in the right place but does not take clicks there. `hittest="false"` on decorative
panels keeps them from eating clicks meant for what is underneath.

Panorama CSS is not web CSS: keep selectors flat, no `&` and no flexbox, and lay out with
`flow-children`. Toggle visibility with a class rather than swapping layouts.

```css
#welcome_card {
 width: 380px;
 horizontal-align: center;
 flow-children: down;
 transition-property: opacity;
 transition-duration: .1s;
 opacity: 1;
}

#welcome_card.hidden { visibility: collapse; opacity: 0; }
```

Valve's own reference layout ships as source at
`csgo_addons/cs_script_demo/panorama_stripped/panorama/layout/custom_game/welcome.xml`, and
`panorama_generate_layout_xsd` on a client dumps the full legal schema for the build you are on.

## Commands

```bash
voltmod build                            # renders panorama/screens/ before configuring
voltmod panorama render [PLUGIN...]       # write layouts, stylesheets, images and headers
voltmod panorama check [PLUGIN...]        # read-only: the checks the client applies silently
voltmod panorama compile [PLUGIN...]      # render, run resourcecompiler, install into your client
```

Omit `PLUGIN` and every plugin that ships a screen renders. Nothing rendered is
committed - a checkout renders before it builds, and CI never needs the CS2 Workshop Tools. `check`
writes nothing and belongs in a consumer repo's lint task; it also refuses two owners writing the
same resource path, a screen whose names cannot be spelled in C++, indexed names that skip an
index or differ between copies, and too many interned names.

`compile` renders first, then compiles with `resourcecompiler.exe` and installs into your own
client. It runs on Windows only.

```bash
uv run poe panorama                          # every plugin that ships a screen
voltmod panorama compile ui-lab              # just one plugin's
voltmod panorama compile --no-deploy         # compile only, leave the client alone
```

It finds the client through Steam's library list; set `CS2_CLIENT_PATH` in `.env` or pass
`--client` when that guess is wrong. Sources are staged into `content/csgo_addons/voltmod/`,
compiled to `game/csgo_addons/voltmod/`, and the compiled resources copied into
`csgo/panorama/{layout,styles}/custom_game/` and `csgo/panorama/images/<set>/`. Reconnect to see
the change; no addon is required for your own client.

## Build tree outputs

```text
build/panorama/<owner>/panorama/layout/custom_game/<name>.xml
build/panorama/<owner>/panorama/styles/custom_game/<name>.css
build/panorama/<owner>/panorama/images/<set>/<icon>.png
build/panorama/<owner>/panorama/images/<set>/<icon>.vtex
build/panorama/<owner>/include/Ui/<Pascal>.hpp
```

`cmake/VoltModPlugin.cmake` adds `build/panorama/<owner>/include` as a private include directory
for a plugin whose source tree has a `panorama/screens/`, so `#include <Ui/<Pascal>.hpp>` resolves
once `voltmod build` has rendered.

## What the header holds

`voltmod panorama render` reads a screen's rendered layout and stylesheet and emits one constant
per name a plugin has to spell.

| In the screen | In the header |
| --- | --- |
| the outermost id | `Name`, the layout name `ScreenManager` takes |
| every other `id="..."` | a `std::string_view` named by what follows the screen prefix - `lab_close` becomes `Close` |
| `text="{s:var}"` | a `std::string_view` named `<Var>Var` |
| a block the template repeats - ids `<screen>_<stem><N>[_<suffix>]` and variables `<stem><N>[_<suffix>]` for N = 0..K-1 (K >= 2), alike in every copy | `struct <Stem>` with one `std::string_view` per member (`Id`, `<Suffix>`, `<Suffix>Var`, or `Var` for a bare variable) and `std::array<<Stem>, K> <Stem>s`; those names get no flat constant. `check` refuses copies that skip an index or differ |
| an `icon-set--<name>` or `bar--<step>` class, in the layout or the stylesheet | `IconSetNames` or `BarNames`, the modifiers in the order they first appear, for `Screen::SetModifier` |

Other modifiers, such as `row--on`, get no constant; a driver writes them as literals.

`tests/Ui/Fixtures/Lab.hpp` is a real header, checked in and regenerated by
`cli/tests/test_panorama_layout.py` so the two never drift. It comes from a screen shaped like the
example above, with two rows:

```cpp
inline constexpr std::string_view Name = "lab";
inline constexpr std::string_view Close = "lab_close";
inline constexpr std::string_view CloseVar = "close";

struct Row { std::string_view Id, Button, Decrease, Increase, LabelVar, HintVar, ValueVar; };
inline constexpr std::array<Row, 2> Rows{ Row{"lab_row0", "lab_row0_button", ...}, Row{"lab_row1", ...} };

inline constexpr std::array<std::string_view, 2> IconSetNames{"ak47", "awp"};
```

Writing one row of it:

```cpp
const LabLayout::Row& row = LabLayout::Rows[index];
screen.SetHidden(slot, row.Id, false);
screen.SetText(slot, row.LabelVar, "Kick");
screen.SetModifier(slot, LabLayout::Icon, "icon-set", LabLayout::IconSetNames, "awp");
```

## Block library

`panorama/blocks/` ships these macros. Import the `.xml.j2` with `{% import %}` and pull in its
default CSS with `{% include "<name>.css.j2" %}`.

| Block | Signature | Draws |
| --- | --- | --- |
| `card` | `card(id, icon_set=none, bar=false)` | a HUD row: an optional icon set, two lines of text, a value, an optional bar along the bottom; starts `hidden`, and the icon takes room only while `card--icon` is on the card |
| `bar` | `bar(id)` | a meter; pair with `bar.css.j2`'s `styles(steps)`, which adds the `bar--step-0`..`bar--step-<steps>` width rules; `hidden` on the bar takes it away |
| `toast` | `toast(id)` | a notice that starts `hidden` and fades in when that class comes off |
| `icons` | `icons(id, set, keep_shape=false)` | one `<Image>` per `(name, src)` pair in `set`, stacked; pair with `icons.css.j2`'s `show_rules(set)` so an `icon-set--<name>` class on the set uncollapses its own image. Icons fill the set's box; `keep_shape` adds `keep-shape`, which sizes each icon from its height so an icon that is not square keeps its shape |
| `button` | `button(id, text, variant="")` | a labelled Button; `variant` adds a `button--<variant>` modifier |
| `dialog` | `dialog(id)`, called not imported | a centred panel with a breadcrumb/title/subtitle header and a body slot |
| `tabs` | `tabs(id, count)` | a strip of hidden-by-default tabs, each reading `{s:<id><i>}` |
| `pager` | `pager(id)` | previous (`_previous`), a `{s:<id>}` label, next (`_next`) |
| `menu` | `menu(tabs, rows, icons, home="")`, used with `{% call %}` | a whole menu: sidebar tabs, header, rows with their switch, chevron and steppers, prompt, Back and a pager; the call body is the sidebar brand, and the screen's `content` holds only this call. Include `menu.css.j2` for its layout. @ref VoltMod::PanoramaMenuLayout draws it |

`dialog` takes its body through `{% call %}` rather than an argument:

```text
{% call shell.dialog("panel") %}
  ...rows, pager, footer...
{% endcall %}
```

A block's CSS carries layout only - sizes, flow, alignment. Colour, radius and state belong to the
screen that includes it, which is what lets two screens share a row and still look different.

Class names are BEM in kebab-case: a block (`row`), its elements (`row__label`) and its modifiers
(`row--disabled`, `button--primary`). A driver writes modifiers too, on the block it knows:
`tab--selected`, `row--on`, `screen--home`. The one class outside a block is `hidden`, which
`Screen::SetHidden` puts on any element and `screen.css.j2` defines once. Ids and dialog variables stay snake_case, since the
header spells them as C++ names.

## Images and icon sets {#panorama_guide_images}

Drop PNGs in an owner's `panorama/images/<set>/`, referenced as
`s2r://panorama/images/<set>/<name>.vtex`. Name a set for its owner (`stronghold`, not
`icons`): a set shares the client's `panorama/images/` with the game's own folders, and one of the
same name replaces them. `png_icons("weapons")` gives the set as `(name, src)` pairs, one per PNG
in file-name order, which is what the `icons` block and `show_rules` take. Rendering copies each PNG
into the build tree and writes a matching `.vtex` descriptor beside it - `resourcecompiler` compiles
the descriptor, never the PNG.

The client's own icons need no files. `game_icons([("gear", "settings"), ...])` gives the same pairs
pointing at `s2r://panorama/images/icons/ui/<icon>.vsvg` (`settings`, `player`, `message`, ...);
`game_icons(pairs, "equipment")` reads `icons/equipment/` instead. Tint them with `wash-color`
and size them in CSS.

## The name budget {#panorama_guide_budget}

The client permanently interns every element id, class name and variable it sees into a 1024-entry
table shared by every screen it loads. `voltmod panorama check` counts the names of all the screens
it builds and refuses the set once it passes 1024, naming the biggest screens first. It also
refuses any single screen past 400 names, which is what a runaway loop looks like rather than a
design. Reuse ids and classes - a block's macros already do - rather than generating a name per
instance.

## Publishing {#panorama_guide_publish}

Other players get the screens from a workshop addon:

1. `voltmod panorama compile [PLUGIN...] --addon NAME --no-deploy` compiles into
   `game/csgo_addons/NAME/` without touching your client.
2. Open NAME in the CS2 Workshop Tools, then the Workshop Manager, and submit it as Public or
   Unlisted. A private item does not download for anyone else.
3. Require the published id from the plugin:

```cpp
if (auto required = runtime.Addons.Require(3401234567))
    _addon = std::move(*required);   // keep the Subscription
```

Delete what an earlier `compile` installed under your client's
`game/csgo/panorama/` before testing the download, or the client keeps using those
files. See @ref workshop_guide for what an addon costs a connecting client.
