# AGENTS.md - foo_bettertabs

Notes for agents working on this component. See the workspace AGENTS.md one level up for the
general rules (file tools, builds through `cmd //c`, v145 toolset, background jobs).

## Build, test, release

- Build: `cmd //c build.bat Release x64` and `cmd //c build.bat Release Win32`. Read
  `build.log` / `build-Win32.log`. The build fails on post-Windows 7 imports on purpose.
- Tests: `cmd //c test\build_tests.bat` (codec, strip layout, strip render, WM_SHOWWINDOW,
  z-order). Results in `test/tests.out`; every EXIT code must be 0.
- Cover colour and contrast (OKLab, APCA) are in the shared `../fb2k-common` library, with its
  own tests (`fb2k-common/test/build_tests.bat`, including a golden test over the user's
  covers). Change them there; EPT, Media Bar and foo_onscreendisplay use the same code.
- Package: `cmd //c package.bat` -> `dist/foo_bettertabs.fb2k-component` (x86 at the root, x64
  in `x64/`) and `dist/symbols/*.pdb`.
- Release: bump `src/version.h`, package, archive the PDBs as
  `../.archive/foo_bettertabs-<version>-symbols.zip`, commit, tag `v<version>`, push, and
  `gh release create` with the .fb2k-component attached.
- Test in a foo_mcp instance (`../foobar2000-component-dev/references/testing-with-foo-mcp.md`):
  `python ../foo_mcp/tools/fb.py install dui64 x64/Release/<dll>`, then drive it. Never claim
  something works because it compiled; tooltips still need the user.

## Things to know

### Showing a tab's panel: ShowWindow, never SWP_SHOWWINDOW

`(Defer)SetWindowPos(SWP_SHOWWINDOW)` never sends `WM_SHOWWINDOW`, and Columns UI's Row/Column
splitters show their own children only from `WM_SHOWWINDOW` (wp TRUE, lp 0). Shown that way, a
splitter tab stayed empty (fixed in 0.5.1, `TabsCore::activate`). Show and hide panel windows
with `ShowWindow`, as Columns UI's Tab stack does. `test/showwindow_test.cpp` proves the
message behaviour. The strip and hot zone are our own windows and may keep using SWP flags.

### Size limits: largest maximum, page capped to its own

`TabsCore::compute_limits` reports min = the largest min of the created tabs and max = the
**largest** max (0.5.4). Columns UI's Tab stack uses the smallest max, so one empty Playlist tabs
(max height = its tab row, or 0) collapsed the whole container to a few pixels. A page whose max is
smaller than the container is placed at its max size at the top left (`TabsCore::child_rect`, used
by `activate` and layout). Uncreated tabs do not count.

### Strip paint failures and first-paint cost

- A failed paint (lost target, no Direct2D) fills the background and invalidates for a full retry
  at most twice, then waits for the next resize or update; the first failure is logged
  (`on_strip_paint_failed`). Invalidating on every failure painted forever and starved the
  thread's `WM_TIMER`s: foobar2000 stopped working (seen in Enhanced Playlist Tabs).
- `sync_size()` re-reads `GetClientRect` in `WM_SIZE` and `WM_PAINT`: the strip once kept 0 x 0
  while its window was 2307 x 44, so it never painted a layout for another size.
- The first `BindDC` of the DC render target costs about 20 ms (cold Direct2D init, once per
  process, at start-up). The switch log's worst-paint breakdown (bind/draw/EndDraw/copy and how long
  before the switch) shows this; it is not a switch cost. Warming it on another thread was rejected.

### Auto-hide windows must stay the topmost children

The hot zone and the overlay strip only work while they are above every panel window in our
z-order. Two things put a panel above them without telling us: `SetParent` back into the
container (places it on top) and a panel's own `SetWindowPos(HWND_TOP)`. Windows sends the parent
no message and **no `EVENT_OBJECT_REORDER`** for either (`test/zorder_test.cpp`). Visualisations
with their own fullscreen mode can do the first when leaving it, which made the strip impossible to reveal (fixed in 0.5.3). `TabsCore` watches
`EVENT_OBJECT_PARENTCHANGE` (one out-of-context hook per process, only while a hot zone exists)
and re-raises in `WM_SETCURSOR` as a fallback for the second. Verified not to bury the hot zone:
D3D11 flip and blt swap chains on the panel, recreating them, toggling the main window topmost.
Layered children need a Windows 8+ manifest in a test exe (`test/compat.manifest`), or
`CreateWindowEx(WS_EX_LAYERED)` on a child fails and the test proves nothing.

### Divider colour in Columns UI

- Row/Column splitters are transparent: the gaps between their panels (the dividers) show what
  the parent paints. Columns UI paints its layout background there, so `TabsCore::fill_background`
  paints `HostColours::layout` for a child's DC and the panel background only for our own DC.
- Columns UI does not expose that colour to components. The values are copied from its source
  (`dark::ColourID::LayoutBackground`): RGB(51, 51, 51) in dark mode, `COLOR_BTNFACE` in light
  mode (`container.cpp`, `host_colours`). If a future Columns UI changes that grey, update it.
- The divider width is Columns UI's own setting (Preferences > Display > Columns UI > Layout, Misc tab; default 2); 0
  hides the dividers everywhere, inside Better Tabs too. Do not add a second width setting.
- An empty Row/Column, or a transparent panel placed directly in a tab, shows the same grey, as
  it would outside Better Tabs.
- Columns UI only. The Default UI container is unchanged (its host leaves `layout` unset, so it
  falls back to the panel background).

### Selection and block drag (0.6)

- Strip items have no keys: the selection is a flag per item and the Shift anchor is an index.
  `set_items` with a different tab count clears the selection (the indices under it moved), and
  any change of the active tab resets the anchor to it. Ctrl/Shift+click takes the focus and
  `clear_selection` gives it back; `WM_KILLFOCUS` ends the selection; the active tab in the
  selection is outlined regardless of `UISF_HIDEFOCUS`. Same design as
  foo_enhancedplaylisttabs 1.3.1; `test/render_test.cpp` `selection_test` covers it.
  `set_labels` keeps the selection by index, so after a reorder the host must rebuild the strip in
  the new order (`TabsCore::on_strip_reorder_block` does) and the selection follows the moved tabs.
- A drag of a selected tab moves the whole selection; each item remembers `Item::drag_origin` so Esc
  can restore the order. The end reports `on_strip_reorder_block(moved, neighbour, before)` with
  original indices. Drags stay inside the visible window (`StripLayout::group_of`).
- Esc during a drag is caught by a short timer polling `GetAsyncKeyState` (the strip has no focus
  while capturing); the timer runs only while dragging.
- The strip background is exactly the host background (no dark-mode lift).
- Tab / outlined-tab indicators share `tab_shape()` with Enhanced Playlist Tabs; keep them in sync.

### Title formatting without a track: never run(nullptr)

`titleformat_object::run` needs a real `titleformat_hook`: the core calls `p_source->process_field`
without a null check, so `run(nullptr, ...)` crashes on the first field (read AV at 0 inside
foobar2000.exe, call path `main_thread_callback::callback_run`). `playback_format_title` returns false
while playback is starting, before the track is open, which is when the fallback ran (fixed in
0.6.2). `update_label` passes `NoTrackHook` instead: with no track, fields are empty (not "?") and a
blank result falls back to the panel's own name; a script with its own `$if()` fallback keeps it.

### Dialog labels in dark mode

Static text is drawn on a transparent background in dark mode. Change a label's text or enabled
state only through `set_label` / `enable` in `configure_dialog.cpp`: they skip no-op changes and
erase the page behind the control first (`repaint_behind`). A plain `SetWindowText` or
`EnableWindow` piles the new text on the old, which looks bold and fringed (fixed in 0.6.1).

### Hover styles (Hover page)

- `Settings::hover_*` style the tabs other than the active one, `active_hover_*` the active tab
  (`StripWindow::hover_mark(active)`; same code as foo_enhancedplaylisttabs). `HoverStyle::plain`
  (active only, the default) is the old wash folded into the tab's own fill. `draw_tab` draws the
  mark (fill, outline via `fill_shape`, or underline) over the tab's own fill. The Hover page
  edits one set at a time ("Settings for:"); writing the controls never reads them first
  (`hover_to_controls`), title combo and title colour included (`HoverFields::text`,
  `text_argb`). The fade is shared by both sets, so it is the first row, above "Settings for:".
- The title of a hovered tab: `hover_text` / `active_hover_text` (`HoverText`: brighten,
  unchanged, the hover colour, a custom colour in `*_hover_text_argb`). Brighten is "to the full
  text colour" for the others and "lighter in OKLab, towards white" for the active tab. Up to 0.9
  the active tab had a bool (`s_active_hover_lighten`); the codec still writes it (brighten = 1)
  before `s_active_hover_text`, which wins.
- Text colours (Colours page): `custom_text` / `text_argb` for the other tabs (else the theme's
  text dimmed), `custom_active_text` / `active_text_argb` for the active tab and, when set, the
  selected tabs. Picked colours (these and the custom hover title) skip the strong-fill contrast
  rescue in `draw_tab` (`chosen`): the user asked for that colour.
- Automatic fill strength (`accent_strength` 0) for a pill or tab is `auto_fill_dark` /
  `auto_fill_light` (`settings.h`, 50 and 40 %; was 30 and 26). Both are at or above
  `strong_fill`, so the active title is checked for contrast against the fill. The Look page's
  slider rests on the matching value (`ConfigureState::dark`). Tests that need the title as drawn
  set `accent_strength` below 40 (the render tests pass it to `StripTheme::active_fill`).
- Transparent background (Colours page): the strip caches the parent's background
  (`DrawThemeParentBackground`) and refetches it on erase, move, size, theme-background changes
  and on any `WM_PAINT` whose rectangle is not inside what the strip invalidated itself
  (`StripWindow::invalidate` keeps `own_dirty_`; never call `InvalidateRect(wnd_, ...)` directly
  in the strip). So a host that repaints its background and invalidates its children without
  `RDW_ERASE` is picked up; a host that never invalidates them can't be. `TabsCore`'s erase
  handler forwards it to the host. Not for the layered auto-hide strip.
- `transparent_opacity` (Colours page, 0 = fully transparent): the strip's background at that
  alpha over the backdrop, one `FillRectangle` of the dirty rect in `render`. `render_test`
  `transparent_test` covers the reuse, the refetch without an erase (real `WM_PAINT`s: the parent
  is shown layered at alpha 1), the opacity and the text colours.
- The Fonts page's Default shows the host size read back from whole pixels with
  `tenths_from_pixels` (`src/model/font_size.h`): 11 px at 96 DPI is 8 pt, not 8.3.
- The fade keeps a `hover_level` per `Item` (so it moves with reorders) and is read only while
  `hover_fading_`; otherwise `index == hover_` decides. Anything that resets `hover_` on an item
  change must call `stop_hover_fade()`.
- `render_test` `hover_test` checks each style by pixels and writes `test/out/hover_96.png`. In
  tests, pump only `WM_TIMER` for the strip: the real pointer is elsewhere, and a posted
  `WM_MOUSELEAVE` ends the hover.
- Pages are `IDD_PAGE_STRIP + i`, so the ids stay consecutive: Strip, Look, Hover, Colours, Fonts,
  then the rest.
- `save_png` in `render_test` writes 24 bpp BGR: WIC's PNG encoder turns a 32 bpp request into
  24 bpp, and the 32 bpp rows it was fed before scrambled every image in `test/out`.

## Performance

- A panel's window is created the first time its tab is shown (can be turned off). Inactive tabs are
  hidden and report themselves as not visible, so well-behaved panels do no work.
- A switch is one `DeferWindowPos` batch: show the new panel, hide the old one. Nothing else is moved
  or repainted.
- No timers, hooks or polling while idle. Auto-hide is event driven (`TrackMouseEvent`); a timer runs
  only while a delay is due, an animation plays, or a tab drag is in progress (to catch Esc).
- Only the strip is painted: double-buffered, dirty rectangles only, and no heap allocations in
  `WM_PAINT` (checked by the render test).
- Measured on a 150 ms switch animation: about 11 strip paints, the slowest about 1 ms, no
  allocations. The first container's own setup takes about 18 ms.
- The DLL does not import Columns UI, so it loads where only the Default UI is installed.

## Tests

`test\build_tests.bat` builds and runs these tests:

- `codec_test`: settings and tabs survive a round trip, fields from newer versions are kept, and
  damaged data falls back to defaults.
- `layout_test`: tab positions for each width mode and alignment, and overflow.
- `render_test`: renders the strip offline, times it, counts allocations in the paint path and
  writes PNGs to `test\out\`. Also checks multi-select and block drag (reorder report, Esc cancel,
  clearing the selection).
- `showwindow_test`, `zorder_test`: the window messages the tab switching relies on.

The cover colour and contrast code has its own tests in `fb2k-common\test\`.

## Source map

| File | Job |
| --- | --- |
| `src/component.cpp` | Component identity |
| `src/hosts/tabs_core.cpp` | The container logic shared by both UIs: tabs, switching, menus, auto-hide |
| `src/hosts/container.cpp` | The Columns UI splitter |
| `src/hosts/dui_container.cpp` | The Default UI container element |
| `src/hosts/configure_dialog.cpp` | The Configure dialog |
| `src/model/settings.cpp`, `codec.cpp` | Settings and their storage |
| `src/model/cover_accent.h`, `colour.h` | Accent colour from the cover (forwarders to `fb2k-common`) |
| `src/model/font_size.h` | Point size behind a GDI font height (Fonts page Default) |
| `src/strip/strip_window.cpp` | The strip: drawing, input, tooltips |
| `src/strip/strip_layout.cpp` | Tab positions and overflow |
| `src/strip/hot_zone.cpp` | The auto-hide hot zone |
| `src/platform/` | Drawing, cover loading and decoding, timing and logging |

Settings are stored in a versioned format that keeps fields it does not know, so adding an option
does not break an existing layout.
