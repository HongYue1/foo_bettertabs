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
