# AGENTS.md - foo_bettertabs

Notes for agents working on this component. See the workspace AGENTS.md one level up for the
general rules (file tools, builds through `cmd //c`, v145 toolset, background jobs).

## Build, test, release

- Build: `cmd //c build.bat Release x64` and `cmd //c build.bat Release Win32`. Read
  `build.log` / `build-Win32.log`. The build fails on post-Windows 7 imports on purpose.
- Tests: `cmd //c test\build_tests.bat` (codec, strip layout, cover accent, strip render,
  WM_SHOWWINDOW). Results in `test/tests.out`; every EXIT code must be 0.
- Package: `cmd //c package.bat` -> `dist/foo_bettertabs.fb2k-component` (x86 at the root, x64
  in `x64/`) and `dist/symbols/*.pdb`.
- Release: bump `src/version.h`, package, archive the PDBs as
  `../.archive/foo_bettertabs-<version>-symbols.zip`, commit, tag `v<version>`, push, and
  `gh release create` with the .fb2k-component attached.
- foobar2000 cannot be run from here: the user tests the DLL in their install.

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
  `set_labels` keeps the selection by index, so after a reorder the host must rebuild the strip in
  the new order (`TabsCore::on_strip_reorder_block` does) and the selection follows the moved tabs.
- A drag of a selected tab moves the whole selection; each item remembers `Item::drag_origin` so Esc
  can restore the order. The end reports `on_strip_reorder_block(moved, neighbour, before)` with
  original indices. Drags stay inside the visible window (`StripLayout::group_of`).
- Esc during a drag is caught by a short timer polling `GetAsyncKeyState` (the strip has no focus
  while capturing); the timer runs only while dragging.
- The strip background is exactly the host background (no dark-mode lift).
- Tab / outlined-tab indicators share `tab_shape()` with Enhanced Playlist Tabs; keep them in sync.

### Dialog labels in dark mode

Static text is drawn on a transparent background in dark mode. Change a label's text or enabled
state only through `set_label` / `enable` in `configure_dialog.cpp`: they skip no-op changes and
erase the page behind the control first (`repaint_behind`). A plain `SetWindowText` or
`EnableWindow` piles the new text on the old, which looks bold and fringed (fixed in 0.6.1).
