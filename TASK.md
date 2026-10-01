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
  Ctrl+Tab, follow playback, hidden tabs menu (PLAN.md section 14). Tested by the user: all pass
  except follow playback (crash), long titles, and startup time.
- **0.3.1**: fixes from that test (PLAN.md section 14, "0.3.1"). Confirmed: no crash, starts/stops
  timing, one tab per event, Remove, rotate default, dialog layout. No Columns UI import in the DLL.
- **0.3.2**: shrink before overflow, Character Map fallback, strip create without throwaway text
  format or tooltip (PLAN.md section 14, "0.3.2"). Built x64 + Win32, tests pass, 0 dialog problems.

## Waiting for

- The user's 0.3.4 cold-start line: own time should drop from ~155 ms to a few ms, with "text
  warm-up NNN ms" (finished on the worker) at the end. If it says "running", the warm-up started
  too late to finish first; then consider an earlier trigger. Cause and numbers: PLAN.md 14, 0.3.4.
- Whether the component loads cleanly in a Default UI-only setup (no Columns UI installed).

## Next

- M(d): auto-hide + animations (an Auto-hide page in the Configure dialog; "Show the strip" gets
  an auto-hide entry). Then M(e) (user left the order to us: M(d) first).
- README (foo_osd style, screenshots), package, skill updates (PLAN.md section 10 plus: reorder_panels
  semantics, Tab stack host behaviour and is_point_ours, transparent children forward
  WM_ERASEBKGND, the CUI Layout page calls show_config_popup on an instance without a window, then
  get_config; ListView vs dark-mode hooks: the hooks replace a report ListView with a list that
  doesn't copy items, so use a list box; play callbacks must not create panels inline, defer with
  `fb2k::inMainThread`).
