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
- **0.3.3-0.3.4**: slow first container traced to a false DPI change in the first WM_SIZE plus cold
  DirectWrite; fixed and warmed on a worker. Confirmed: own time 155 -> 18 ms, startup 0.536 ->
  0.225 s.
- **0.4.0 beta 1** (M(d) step 1): auto-hide (PLAN.md section 14, "0.4.0 beta 1"). Built x64 +
  Win32, 0 warnings, tests pass, 0 dialog problems. Confirmed by the user. Beta 2: defaults slide
  animation and 6 DIP hot zone.
- **0.4.0 beta 3** (M(d) step 2): tab switch animation, indicator slide + colour cross-fade
  (PLAN.md section 14, "0.4.0 beta 3"). Confirmed by the user. Beta 4: text colours change at once,
  only the indicator slides. Confirmed. **0.4.0 released (M(d) done).**
- **0.5.0 beta 1** (M(e)): `TabsCore` refactor (CUI host now ~700 lines) and the Default UI
  element (PLAN.md section 14, "0.5.0 beta 1"). Built x64 + Win32, 0 warnings, tests pass, 0
  dialog problems. Confirmed by the user (CUI regression, DUI editing and use; clean console in a
  portable Default UI install). **0.5.0 released (M(e) done).**

## Waiting for

- Optional: the 16.9 ms still in "after WM_CREATE" of the first container (not chased).

## Next

- README (foo_osd style, screenshots), package, skill updates (PLAN.md section 10 plus: reorder_panels
  semantics, Tab stack host behaviour and is_point_ours, transparent children forward
  WM_ERASEBKGND, the CUI Layout page calls show_config_popup on an instance without a window, then
  get_config; ListView vs dark-mode hooks: the hooks replace a report ListView with a list that
  doesn't copy items, so use a list box; play callbacks must not create panels inline, defer with
  `fb2k::inMainThread`).
