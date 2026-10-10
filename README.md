<h1 align="center">Better Tabs (foo_bettertabs)</h1>

<p align="center">
  A tab container for <a href="https://www.foobar2000.org/">foobar2000</a> v2, for Columns UI and
  Default UI: one tab per panel or element.
</p>

<p align="center">
  <img src="docs/images/screenshot_1.png" alt="Better Tabs with a side strip of rotated tab titles and a yellow accent taken from the cover" width="800" />
</p>

## Features

- **Works in both UIs**: a splitter in Columns UI and a container element in Default UI, with the
  same strip, menus and settings.
- **Strip on any side**, with rotated titles on the left and right if you like.
- **Your look**: underline, pill, tab, outlined tab or text-only indicator, chips, corner radius,
  tab width and alignment, spacing, hover styles (the active tab has its own), text colours, a tab
  font with fallbacks, and transparency with an adjustable background opacity.
- **Icons on tabs** (Segoe Fluent Icons or emoji), or icon-only tabs.
- **Accent colour** from the UI, a custom colour or the playing track's cover, and a strip
  background that can be tinted with it. Colours, fonts and dark mode follow your Columns UI or
  Default UI settings.
- **Titles**: the panel's own name, your text, or title formatting.
- **Switching** by click, mouse wheel, Ctrl+Tab, the tab list, or automatically when playback
  starts or stops.
- **Reorder, hide and multi-select** tabs; drag a selection as one block.
- **Auto-hide**: the strip shows when the pointer reaches the edge.
- **Light on resources**: a panel is created the first time its tab is shown, hidden tabs do no
  work, and nothing runs while idle.

## Install

Requires foobar2000 v2 on Windows 7 or later, 32-bit or 64-bit (the package contains both).
Columns UI is optional.

1. Download `foo_bettertabs.fb2k-component` from the
   [latest release](https://github.com/HongYue1/foo_bettertabs/releases/latest).
2. Double-click it, or in foobar2000 open **Preferences > Components > Install...**, and restart.
3. Add it: in Columns UI, **Preferences > Display > Columns UI > Layout**, add
   **Splitters > Better Tabs** and add panels under it; each panel is a tab. In Default UI, enable
   **View > Layout > Enable layout editing mode**, then right-click > **Replace UI Element... >
   Containers > Better Tabs**. It starts with one empty tab; click it to pick an element.

Settings are under **Configure...** in the strip menu. They belong to each container, preview live,
and **Cancel** undoes them.

## Keyboard and mouse

| Key / mouse | Action |
| --- | --- |
| Click a tab | Show it |
| Ctrl+click / Shift+click | Add a tab to or take it out of the selection / select a range |
| Click empty strip space, Esc | Clear the selection |
| Mouse wheel | Previous / next tab |
| Ctrl+Tab / Ctrl+Shift+Tab | Next / previous tab, while the focus is inside the container |
| Middle click | Nothing, or hide the tab (Behaviour page) |
| Drag a tab | Reorder; a selected tab brings the whole selection along; Esc cancels |
| Right-click the strip | Tab list, rename, hide, move, show hidden tab, Appearance, Configure |

In Default UI layout editing mode, right-click the strip for **Add new tab**, **Paste as new tab**
and, on a tab, **Replace**, **Copy**, **Rename** and **Remove**. Right-click an element inside for
**Rename tab**, **Remove tab** and **Configure Better Tabs**.

## Good to know

- **Tab titles**: Rename with an empty title goes back to the panel's own name. Title formatting
  is optional per tab (see **Help** on the Tabs page).
- **Icons**: enter a code point (such as `E8D6`) or paste a character. Copy Segoe Fluent Icons
  from Character Map and emoji from Win+. (period).
- **Tabs that follow playback**: on the Tabs page, a tab can be shown when playback starts or
  stops.
- **Size in Columns UI**: the container is at least as large as its largest panel minimum and can
  grow to the largest panel maximum. A panel with a smaller maximum keeps its own size at the top
  left instead of shrinking the whole container (Columns UI's Tab stack uses the smallest maximum).
- **Missing elements** show *(missing element)* and keep their settings, so they come back with
  their component. Removing the last tab leaves an empty one.
- **In Columns UI** the strip menu also has the panel's own items, and FCL export and import keep
  the container's settings.
- **Auto-hide** keeps the strip while its menu is open, while you drag a tab and while it has the
  keyboard focus.
- **Transparency** only shows something when your layout draws a background behind the strip,
  such as a Columns UI theme. The auto-hide strip shown over the panel stays solid. If the
  container changes its background (a new cover, say), it has to repaint the panels inside it;
  the strip picks up the new background on that repaint.
- **Text colours**: the Colours page sets the titles of the other tabs and of the active (and
  selected) tabs; the Hover page sets a hovered tab's title, separately for the active tab.
  Colours you pick are used as they are. "Brightens" on the active tab lightens towards white,
  which does nothing to white text.
- **Performance log**: **Preferences > Advanced > Display > Better Tabs: log performance to the
  console**.

## Building

Visual Studio 2022 or later with the C++ desktop workload and ATL, next to these folders:
`SDK-2026-09-17` (foobar2000 SDK, with the Columns UI SDK inside as `columns_ui-sdk`), `wtl`
(WTL 10) and [`fb2k-common`](https://github.com/HongYue1/fb2k-common).

- `build.bat [Release|Debug] [x64|Win32]` builds the DLL.
- `test\build_tests.bat` builds and runs the tests (no foobar2000 needed).
- `package.bat` builds both platforms into `dist\foo_bettertabs.fb2k-component` (PDBs in
  `dist\symbols`, needs 7-Zip).

The Visual Studio and 7-Zip paths are at the top of the batch files.

## See also

My other components:

- [foo_enhancedplaylisttabs](https://github.com/HongYue1/foo_enhancedplaylisttabs): playlist tabs
  built on the same strip.
- [foo_filetree](https://github.com/HongYue1/foo_filetree): a folder tree panel.
- [foo_onscreendisplay](https://github.com/HongYue1/foo_onscreendisplay): an on-screen display.

## License

[MIT](LICENSE)
