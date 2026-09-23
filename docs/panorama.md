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
{# namespace: ArenaLayout #}
{% import "button.xml.j2" as controls %}
{% import "icons.xml.j2" as icons %}
{% import "listrow.xml.j2" as list %}
<root>
  <styles>
    <include src="file://{resources}/styles/custom_game/{{screen}}.css" />
  </styles>
  <Panel class="layer" hittest="false">
    <Panel id="{{screen}}" class="screen hidden" hittest="false">
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
.screen {
  width: 420px;
  flow-children: down;
}

.screen.hidden {
  visibility: collapse;
}

{% include "listrow.css.j2" %}
{% include "button.css.j2" %}

{{ icons.show_rules("weapons") }}
```

The template context is `screen` (the file's own name) and `images` (every icon set the owner
ships). Block macros come in with `{% import %}`; their default CSS comes in with `{% include %}`,
once, in the screen's own stylesheet. `{# namespace: X #}` as the template's first line names the
header's C++ namespace; without it the namespace is `Screens::<Pascal>` (`hud.xml.j2` gives
`Screens::Hud`).

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
- The stylesheet is included by its **source** name under `{resources}`, not the compiled
  `.vcss_c` name.
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
same resource path, a screen whose names cannot be spelled in C++, and too many interned names.

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
| the outermost id | `Layout` and `RootId` |
| every other `id="..."` | a `std::string_view` named by what follows the screen prefix - `lab_close` becomes `Close` |
| `text="{s:var}"` | a `std::string_view` named `<Var>Var` |
| a block the template repeats - ids `<screen>_<stem><N>[_<suffix>]` and variables `<stem><N>[_<suffix>]` for N = 0..K-1 (K >= 2), alike in every copy | `struct <Stem>` with one `std::string_view` per member (`Id`, `<Suffix>`, `<Suffix>Var`, or `Var` for a bare variable) and `std::array<<Stem>, K> <Stem>s`; those names get no flat constant |
| a `block--modifier` class, in the layout or the stylesheet | a `BlockClasses` array, and a `BlockNames` array of the modifiers alone in the same order; `icon-set__icon--awp` belongs to `IconSetIcon` |

A family is found by a plain scan for `.block--modifier`, layout order first and then stylesheet
order. The stylesheet half matters: a family only selectors use is declared nowhere in the layout.

`tests/Ui/Fixtures/Lab.hpp` is a real header, checked in and regenerated by
`cli/tests/test_panorama_layout.py` so the two never drift. It comes from a screen shaped like the
example above, with two rows:

```cpp
inline constexpr std::string_view RootId = "lab";
inline constexpr std::string_view Close = "lab_close";
inline constexpr std::string_view CloseVar = "close";

struct Row { std::string_view Id, Button, Decrease, Increase, LabelVar, HintVar, ValueVar; };
inline constexpr std::array<Row, 2> Rows{ Row{"lab_row0", "lab_row0_button", ...}, Row{"lab_row1", ...} };

inline constexpr std::array<std::string_view, 2> IconSetClasses{"icon-set--ak47", "icon-set--awp"};
inline constexpr std::array<std::string_view, 2> IconSetNames{"ak47", "awp"};
```

Writing one row of it:

```cpp
const LabUi::Row& row = LabUi::Rows[index];
screen.SetHidden(slot, row.Id, false);
screen.SetText(slot, row.LabelVar, "Kick");
```

## Block library

`panorama/blocks/` ships these macros. Import the `.xml.j2` with `{% import %}` and pull in its
default CSS with `{% include "<name>.css.j2" %}`.

| Block | Signature | Draws |
| --- | --- | --- |
| `card` | `card(id, icon_set=none, bar=false)` | a HUD row: an optional icon set, two lines of text, a value, an optional bar along the bottom; starts `hidden`, and the icon takes room only while `card--icon` is on the card |
| `bar` | `bar(id)` | a meter; pair with `bar.css.j2`'s `fill_rules(cls, steps)` for the `bar--step-0`..`bar--step-<steps>` width rules; `hidden` on the bar takes it away |
| `toast` | `toast(id)` | a notice that starts `hidden` and fades in when that class comes off |
| `icons` | `icons(id, set)` | one `<Image>` per PNG in the icon set, or per `(name, icon)` pair of client icons, stacked; pair with `icons.css.j2`'s `show_rules(set)` so an `icon-set--<name>` class on the set uncollapses its own image |
| `button` | `button(id, text, variant="")` | a labelled Button; `variant` adds a `button--<variant>` modifier |
| `dialog` | `dialog(id)`, called not imported | a centred panel with a breadcrumb/title/subtitle header and a body slot |
| `listrow` | `listrow(id, switch=false, hint=false, value=true, steppers=false, chevron=false)` | one row of a list: two lines of text, a value, a collapsed switch and chevron the screen shows per row class, and steppers; its ids end `_button`, `_decrease` and `_increase` |
| `tabs` | `tabs(id, count)` | a strip of hidden-by-default tabs, each reading `{s:<id><i>}` |
| `pager` | `pager(id)` | previous (`_previous`), a `{s:<id>}` label, next (`_next`) |
| `menu` | `menu(tabs, rows, icons)`, used with `{% call %}` | a whole menu screen, root panel included: sidebar tabs, header, rows, prompt, Back and a pager; the call body is the sidebar brand, and `<root>` holds only the styles and this call. @ref VoltMod::PanoramaMenuLayout draws it |

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
`Screen::SetHidden` puts on any element. Ids and dialog variables stay snake_case, since the
header spells them as C++ names.

## Images and icon sets {#panorama_guide_images}

Drop PNGs in an owner's `panorama/images/<set>/`, referenced as
`s2r://panorama/images/<set>/<name>.vtex`. Name a set for its owner (`stronghold`, not
`icons`): a set shares the client's `panorama/images/` with the game's own folders, and one of the
same name replaces them. Every set becomes an entry in the `images` context (`images.weapons`,
sorted file stems), so `{% for name in images[set] %}` in a block can draw one `<Image>` per icon. Rendering copies each PNG into the build tree and writes a
matching `.vtex` descriptor beside it - `resourcecompiler` compiles the descriptor, never the PNG.

The client's own icons need no files. Point an `<Image>` at
`s2r://panorama/images/icons/ui/<name>.vsvg` (`settings`, `player`, `message`, ...) and tint it with
`wash-color`, sizing them in CSS. The `icons` block does both when `set` is a list of
`(name, icon)` pairs.

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
