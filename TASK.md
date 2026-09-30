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

## Waiting for

- The user's M(a) test run (checklist in the M(a) hand-off message) and the console perf block.

## Next

- M(b): dark-aware overflow/strip menus (verify native menus follow CUI dark mode first), accent
  from cover (port foo_mediabar OKLab code + accent_test), `cui::fonts::get_font` DWrite path,
  per-monitor DPI checks, render test to PNG at 100/150/200%.
- M(c): Configure dialog, title-format titles, icons, middle click, drag reorder, Ctrl+Tab filter,
  follow playback.
- M(d): auto-hide + animations.
- Skill updates listed in PLAN.md section 10, once the user's test confirms them.
