# The brief (verbatim)

Later decisions (name `foo_bettertabs` / "Better Tabs", Q1-Q6) are in PLAN.md section 11.

---

Build foo_bettertab: a modern tab container for foobar2000 Columns UI. It holds other panels,
one visible at a time, and replaces the built-in "Tab stack".

Before writing code, read AGENTS.md, then the foobar2000-component-dev and columns-ui-sdk skills,
including every reference file. The sdk-quirks.md and wtl-quirks.md files are mandatory.
Follow foo_mediabar's repo layout, build.bat / package.bat, versioning, commit style and
never-throw boundaries. Build x86 and x64 against SDK-2026-09-17, CUI SDK 8.1.0 and WTL 10.01.
Windows 7 is the floor: no _WTL_DPI_SUPPORT, and no hard imports of post-Win7 APIs.

## What it is
- A uie::splitter_window_v3 (type_layout | type_splitter, category "Splitters"), multi-instance.
It must show up in the Layout tree like Tab stack, with children added, removed, replaced and
reordered from the Layout page context menu. Implement reorder_panels too.
- The host side must follow every window_host rule in the header: stable host GUID, correct
is_visible / set_window_visibility, all-or-nothing request_resize, forwarding of
WM_SETTINGCHANGE / WM_SYSCOLORCHANGE / WM_TIMECHANGE, no dialog manager, and Tab-key
navigation via g_on_tab.
- Per-child data (custom title, icon glyph, hidden flag) goes in splitter_item_full_v3 extra
data. Version that data, and version the instance blob in set_config/get_config. Both must
survive an FCL export/import round-trip and a downgrade.

## Priority 1: absolute performance
- Idle cost is zero: no timers, no polling, no work when nothing changes. Animation timers
exist only while an animation runs.
- Inactive tabs are hidden, never repainted, and report is_visible = false, so well-behaved
children (visualisations, spectrum, lyrics) pause.
- Create each child's window lazily, on first activation. An option keeps it eager.
Once created, a child is never destroyed just because you switched tabs.
- Switching tabs means one DeferWindowPos batch, no flicker and no parent-background
repaint storm. Use WS_CLIPCHILDREN / WS_CLIPSIBLINGS and wrap the switch in WM_SETREDRAW
if that measurably helps.
- The tab strip is the only thing we paint. Double-buffered, only the invalidated tab rects,
with cached text layout and metrics recomputed only on font, DPI or title change.
No allocations in WM_PAINT.
- Hover uses TrackMouseEvent. No hit-testing loops, no global hooks.
- Measure it: log switch latency and strip paint time behind a debug flag. Report numbers for
20 tabs, including the time from the first Artwork view switch to its first paint.

## Priority 2: modern appearance
- A flat strip that follows Columns UI colours and dark mode: a cui::colours::client with
bool_flag_dark_mode_enabled, and a cui::fonts::client for the tab font. Redraw live when
either changes.
- Active indicator styles: underline bar, pill, or none, with an accent colour. The accent can
come from the CUI selection colour, a custom colour, or the playing track's cover (reuse
foo_mediabar's OKLab accent code).
- Rounded corners, hover highlight, optional close-free "chip" look, and crisp text on whole
pixels. The overflow chevron opens a dark-mode-aware menu. Correct at 100%, 150% and 200%
DPI and across per-monitor DPI moves.
- Optional short animations (indicator slide, cross-fade of the strip only, never of the
children). The "off" setting must cost nothing.

## Priority 3: high customisability (per instance, in a Configure dialog)
- Strip position top / bottom / left / right. On left/right, text is horizontal or rotated.
- Tab sizing: fit to text, equal width, or fill. Alignment left / centre / right.
Padding, spacing and height, all in DIPs.
- Show the strip: always, never, only when there are two or more tabs, or auto-hide
(see the auto-hide section).
- Per-tab custom title and hidden flag. Title can be a title-format string; recompile it only
when the string changes.
- Mouse: wheel over the strip cycles tabs, middle-click is configurable, and drag reorders
(it must call reorder_panels so the Layout tree stays in sync).
- Keyboard: Ctrl+Tab / Ctrl+Shift+Tab while focus is inside, and arrow keys on a focused strip.
- Remember the last active tab per instance. Optionally follow playback: switch to a chosen
tab on play and another on stop.
- The Configure dialog follows references/preferences-pages.md (native look, dark mode,
DU grid, dialog_check.bat) and previews changes live.

## Auto-hide tab strip (a first-class feature)
- Per-instance setting "Tab strip: always shown / auto-hide". Default: always shown.
- When auto-hide is on, the strip is hidden and the content gets the full panel area.
The strip appears when the pointer enters a thin hot zone on the strip's edge
(default 4 px in DIPs; top/bottom/left/right follows the strip position).
It hides again once the pointer has left both the strip and the hot zone.
- Reveal delay (default 0 ms) and hide delay (default 400 ms) are both configurable, so a
quick flick past the edge doesn't flash the strip and moving to a tab doesn't hide it.
- Reveal mode: "overlay" draws the strip over the content, so the child is not resized or
re-laid out. That's the default, and the one to use for performance. "Push" shrinks the
content while the strip is shown.
- Overlay must not force the child to repaint when the strip hides. Implement it as a
separate layered/topmost-in-parent child or an equivalent that doesn't invalidate the child.
Measure it: no child repaint on show/hide.
- Keep the strip visible while a menu from it is open, during drag-reorder, while the
keyboard focus is on the strip, and for a moment after a tab switch, so the user sees what
they picked.
- Optional slide/fade animation for show/hide. With animations off, it's an instant toggle
at zero cost.
- Hover detection uses TrackMouseEvent / WM_MOUSELEAVE on the hot zone and the strip.
No polling, no global mouse hooks. The only timers are the single reveal/hide delay timer
and the animation timer while one runs.
- Clicking a tab switches, as usual. Hovering never switches tabs.

## Deliverables and workflow
1. Plan first, in PLAN.md: the interfaces you'll implement, the config blob layout, the paint
pipeline, and a list of the header rules that apply. Wait for my OK before coding.
2. Build in milestones:
(a) container with a plain strip, lazy children, and persistence;
(b) appearance and the colour/font clients;
(c) options and the Configure dialog;
(d) auto-hide strip and animations
Build both architectures after each milestone and give me a test checklist.
3. Test cases: two instances side by side; nesting a "better tab" inside another; Artwork view,
ESLyric, Item properties and Album list as children; layout switch, FCL round-trip,
dark/light toggle and DPI change all live; 0, 1 and 30 children.
4. README with screenshots in the foo_osd style, and a .fb2k-component package.
5. Anything you learn that isn't in the skills yet goes into the skills, with header
references.

Out of scope for v1, but the design must not block it: a Default UI container element
sharing the same strip renderer.
