# Handoff state

Read PROMPT.md (brief), PLAN.md (design, decisions in section 11), then this file.

## Done

- **M(a) 0.1.0**: container (`src/hosts/container.cpp`), host, lazy/eager children, one
  `DeferWindowPos` per switch, strip window (`src/strip/`) with D2D DC target into a DIB, dirty-rect
  painting, hover via `TrackMouseEvent`, wheel/arrows/Home/End, overflow chevron + menu, strip
  right-click menu (tab list + the child's own menu items), blob + tab extra codec, FCL
  import/export through the children, `reorder_panels`, config items (custom title, hidden),
  colour and font clients, perf logging (advconfig Display > "Better Tabs: log performance...").
  Builds x64 and Win32, imports clean, offline tests pass (`test/build_tests.bat`).
- **0.1.1**: Tab stack `is_point_ours` semantics; container paints background for transparent
  children. Confirmed by the user.
- **M(b) 0.2.0-0.2.2**: appearance (PLAN.md section 13). Confirmed by the user.
- **M(c) 0.3.0**: Configure dialog, rename, icons, title formatting, middle click, drag reorder,
  Ctrl+Tab, follow playback, hidden tabs menu (PLAN.md section 14).

## Waiting for

- The user's M(c) test run, including screenshots of every Configure page in light and dark mode.

## Next

- M(d): auto-hide + animations (an Auto-hide page in the Configure dialog).
- README (foo_osd style, screenshots), package, skill updates (PLAN.md section 10 plus: reorder_panels
  semantics, Tab stack host behaviour and is_point_ours, transparent children forward
  WM_ERASEBKGND, the CUI Layout page calls show_config_popup on an instance without a window, then
  get_config; ListView vs dark-mode hooks).
