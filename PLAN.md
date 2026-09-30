# foo_bettertab - plan

A modern tab container for Columns UI. It hosts other panels, shows one at a time, and replaces the
built-in *Tab stack*. Status: **plan, waiting for approval. No code yet.**

Ground truth: `SDK-2026-09-17/` (fb2k SDK 20260917), `SDK-2026-09-17/columns_ui-sdk/` (CUI SDK 8.1.0),
`wtl/Include` (WTL 10.01). Where it helped, the real Tab stack source was read too:
`foo_nowbar/lib/columns_ui/foo_ui_columns/splitter_tabs.cpp` (reupen/columns_ui @ 2621887).

## 1. Goals, in priority order

1. **Performance.** Zero idle cost. Hidden tabs are really hidden and say so. Lazy children. One
   `DeferWindowPos` per switch. We paint only the strip, double-buffered, dirty rects only, no
   allocations in `WM_PAINT`. Measured, not assumed.
2. **Modern look.** Flat strip in CUI colours, fonts and dark mode; underline / pill / none
   indicator; accent from CUI selection, custom, or cover (OKLab, from foo_mediabar). DPI-correct
   at 100/150/200% and across per-monitor moves.
3. **Customisation.** Per instance, in a Configure dialog with a live preview.
4. **Auto-hide strip**, a first-class feature: overlay by default, no child repaint on show/hide.

Out of scope for v1, but not blocked: a Default UI container sharing the strip renderer (see
section 4, layering).

## 2. Header rules that apply

Every rule below is quoted from a header (or the CUI source where marked) and has a design answer.

| # | Rule | Source | Our answer |
| --- | --- | --- | --- |
| H1 | Host must not be dialog managed | `window_host.h:16` | No `IsDialogMessage`, no `modeless_dialog_manager` on the container. Tab keys handled by hand (H6). |
| H2 | Host must forward `WM_SETTINGCHANGE`, `WM_SYSCOLORCHANGE`, `WM_TIMECHANGE` to hosted windows | `window_host.h:18-21` | Children are **direct children** of the container window, so `container_window_v3` forwards to all of them, hidden ones included (`container_window_v3.cpp:70-73`). The strip is a direct child too and uses `WM_SETTINGCHANGE` to refresh ClearType params. The Win7 overlay popup (5.4) is not a child; we forward to it by hand. |
| H3 | `request_resize` is all-or-nothing | `window_host.h:62-64` | Only for the **active** child. Translate the child size to ours (add strip thickness on the strip axis), ask our host with the same flags if our host's `is_resize_supported()` covers **all** of them, else return `false` and change nothing. Inactive child: `false`. (Tab stack always returns `false`; we do better but stay inside the rule.) |
| H4 | `is_visible` may be false while `WS_VISIBLE` is set | `window_host.h:128-136` | `is_visible(w) = our host->is_visible(us) && w is the active tab && strip/auto-hide state does not cover it`. Inactive tabs are `SW_HIDE`d as well, so `IsWindowVisible` agrees and children that check it (foo_mediabar does) pause. |
| H5 | `is_visibility_modifiable` / `set_window_visibility` | `window_host.h:138-160` | Same semantics as Tab stack (`splitter_tabs.cpp:92-126`): hiding is never allowed (`false`); showing means "switch to this tab", after asking our own host to show us if we are hidden. |
| H6 | Tab key must work: `g_on_tab()` when neither the shortcut manager nor a dialog manager took `VK_TAB` | `window_host.h:94-111`, `window.h:273` | Strip `WM_KEYDOWN`: shortcut manager first (only if `get_keyboard_shortcuts_enabled()`), then `VK_TAB` -> `uie::window::g_on_tab(strip)`. Container has `WS_EX_CONTROLPARENT`; strip has `WS_TABSTOP`. |
| H7 | Hosted window: `WS_CHILD`, no `WS_POPUP`/`WS_CAPTION`/`WS_VISIBLE`, dialog ID 0, `get_my_instance()` | `window.h:176-190` | Via `container_uie_window_v3_t<uie::splitter_window_v3>`. |
| H8 | Size limits come from `WM_GETMINMAXINFO`, `get_size_limits` is unused | `window.h:223` | Container answers `WM_GETMINMAXINFO`: max of mins / min of maxes over **created** children (Tab stack does the same, `splitter_tabs.cpp:425-445`), plus strip thickness in push/always mode. `on_size_limit_change` from a child re-queries it, recomputes, forwards to our host. A lazily created child changing the limits triggers the same path. |
| H9 | Host GUID identifies the host type | `window_host.h:25-32` | One fresh static GUID for our host class, constant across instances (Tab stack does this). |
| H10 | `insert/remove/replace/get_panel` may be called with no window | `splitter.h:336-355` | Panel list lives in plain members (`std::vector<Tab>`); HWND is optional state. |
| H11 | `get_panel()` returns an owned pointer | `splitter.h:350-358` | Return `new splitter_item_full_v3_impl_t`, filled (GUID, **live** child config if created, title, `m_hidden`, extra data). |
| H12 | `set_panel_guid` wipes config and window ptr | `splitter.h:20` | Our `replace_panel` destroys the old child window first, then takes GUID, config and extra data from the item. |
| H13 | Extra data: check the format ID; it can travel between instances via the clipboard and must tolerate format changes | `splitter.h:165-190` | Own format GUID; versioned blob (3.2). A foreign or unknown format ID: fall back to `splitter_item_full_t` title / `m_hidden`, else defaults. |
| H14 | `reorder_panels`: `count == get_panel_count()`; valid with and without a window | `splitter.h:477-490` | **Semantics differ from the header wording.** The header says "new positions for each panel"; CUI's own Tab stack does `new[i] = old[order[i]]` (`splitter_tabs.cpp:852-858`), i.e. `order[new] = old`. We follow CUI (it's the caller). Goes into the skill. |
| H15 | `set_config`/`get_config` may throw `pfc::exception`; nothing else may throw | `base.h:44,61`; skill | `set_config` never throws on bad data: it reads defaults. Only a real stream failure propagates, as `pfc::exception`. Every other virtual, wndproc, callback: `noexcept` + catch-all at the boundary (foo_mediabar pattern). |
| H16 | `set_config` only before the window exists | `base.h:36` | Settings changed later come from our own Configure dialog, applied directly. |
| H17 | Splitter callbacks are no-ops | `splitter.h:344-347` | Not used. |
| H18 | `on_bool_changed` carries a mask, not values | `colours.h:105` | Re-read `is_dark_mode_active()`. |
| H19 | FCL feedback: import reports only missing panels, export reports all | `columns_ui.h:240-250` | `import_config`/`export_config` walk children and forward to each child's own `import_config`/`export_config`, and report child GUIDs. |
| H20 | Custom title as config items | `splitter.h:283-293`; Tab stack `splitter_tabs.cpp:300-330` | `get_config_item_supported` for `bool_use_custom_title`, `string_custom_title` and `bool_hidden`, so the Layout page's own title/hide commands work on our children. |
| H21 | `splitter_window_v2`: `is_point_ours`, `get_supported_panels` | `splitter.h:434-470` | Implemented, so live layout editing can reach children. `get_supported_panels` calls `is_available(our host)` per window. |
| B1 | `inMainThread` queues, `inMainThread2` doesn't | fb2k `sdk-quirks.md` | Cover accent result comes back with `inMainThread`. |
| B2 | No `play_callback_impl_base` statically; no replay on register | `play_callback.h:78-88` | One lazily created shared play callback, registered only while an instance needs it (title-format titles with track fields, follow-playback, cover accent); reads current state on register. |
| B3 | `now_playing_album_art_notify_manager::add(std::function)` leaks with `remove()` | fb2k `entry-points.md` | Inherit `now_playing_album_art_notify`; add/remove `this`. |
| B4 | Win7 floor: no static import of post-Win7 APIs | fb2k skill | `GetDpiForWindow`, `AdjustWindowRectExForDpi`, `SystemParametersInfoForDpi` via `GetProcAddress`. No `_WTL_DPI_SUPPORT`. `dumpbin -imports` check in `build.bat` (fails the build on a hit). |

## 3. Interfaces and classes

```text
bettertab::container            : uie::container_uie_window_v3_t<uie::splitter_window_v3>
                                    (+ uie::menu_window? no - see 6.3)
  type_layout | type_splitter, category "Splitters", name "Better tab"
  static uie::window_factory<container>          multi-instance
bettertab::container::host      : uie::window_host_ex   (per instance, fb2k::service_new,
                                    back pointer cleared in destroy_window)
  static uie::window_host_factory<host>          so CUI can enumerate the host type
bettertab::colours_client       : cui::colours::client  (bool_flag_dark_mode_enabled, themes: no)
bettertab::font_client          : cui::fonts::client    ("Better tab: tabs", default = labels)
bettertab::play_hub             : play_callback + now_playing_album_art_notify, refcounted by
                                    the instances that need it; not registered otherwise
bettertab::configure_dialog     : CDialogImpl, child-per-tab, CDarkModeHooks, live preview
advconfig: "Better tab: log performance" (checkbox, off)
```

`splitter_window_v3` methods: `insert_panel`, `remove_panel`, `replace_panel`, `get_panel_count`,
`get_panel`, `get_config_item_supported` / `get_config_item` / `set_config_item` (H20),
`is_point_ours`, `get_supported_panels`, `reorder_panels`. `get_maximum_panel_count` stays infinite.

### Per-tab state (`Tab`)

```text
GUID panel_guid; pfc::array_t<uint8_t> panel_config;   // authoritative while window is null
uie::window::ptr window; HWND wnd;                     // null until first activation
TabExtra extra;                                        // 3.2
// derived, rebuilt only on change:
compiled title script, display text, text layout, measured width
bool size_stale;                                       // resized lazily on activation
```

## 3.1 Instance blob (`set_config` / `get_config`)

Little-endian. Designed to survive FCL round-trips **and downgrades**: unknown data is skipped, never
fatal, never discarded on the next save if we can avoid it.

```text
header   'B' 'T' 'A' 'B'            magic
         uint16 format_version       1 in v1; bumped only if the header itself changes
         uint16 min_reader_version   1; a reader older than this reads defaults for settings
                                     (children are still read - they are in their own section)
sections repeated until end of stream:
         uint16 tag, uint32 length, length bytes
  tag 1  SETTINGS  field list: repeated (uint16 field_id, uint16 len, bytes). Unknown ids skipped;
                   missing ids keep defaults. Enums clamped on read.
  tag 2  CHILDREN  uint32 count, then per child:
                   GUID, uint32 cfg_len, cfg bytes, uint32 extra_len, extra bytes (3.2)
  tag 3  STATE     uint32 active_index (remembered tab)
  other  kept verbatim in `unknown_sections` and written back on get_config, so a downgrade
         followed by a save does not strip a newer build's data
```

Rules: empty stream = defaults (new instance). Wrong magic = defaults. A section whose length runs
past the stream end = stop reading, keep what was read. `get_config` pulls **live** config from created
children (`get_config_to_array`), stored config otherwise. `export_config`/`import_config` do the same
but through each child's `export_config`/`import_config` (H19).

Settings fields (ids fixed forever once shipped; DIP values are uint16):

| id | field | default |
| --- | --- | --- |
| 1 | strip position: top/bottom/left/right | top |
| 2 | side text: horizontal / rotated | horizontal |
| 3 | sizing: fit / equal / fill | fit |
| 4 | alignment: left / centre / right | left |
| 5-7 | padding x, padding y, spacing (DIP) | 12, 6, 2 |
| 8 | strip height/width (DIP, 0 = from font) | 0 |
| 9 | show strip: always / never / 2+ tabs / auto-hide | always |
| 10 | indicator: underline / pill / none | underline |
| 11 | accent source: CUI selection / custom / cover | CUI selection |
| 12 | custom accent 0xAARRGGBB | 0xFF3EA6FF |
| 13 | rounded corner radius (DIP) | 4 |
| 14 | chip look (on/off) | off |
| 15 | animations: off / on; duration ms | off, 150 |
| 16 | wheel cycles tabs | on |
| 17 | middle click: nothing / hide tab / remove tab / configure child | nothing |
| 18 | drag reorder | on |
| 19 | lazy children (off = eager) | on |
| 20 | remember last tab | on |
| 21 | follow playback: on-play tab index, on-stop tab index (0xFFFF = none) | none, none |
| 22 | auto-hide: hot zone DIP, reveal ms, hide ms, linger-after-switch ms | 4, 0, 400, 700 |
| 23 | auto-hide reveal mode: overlay / push | overlay |
| 24 | auto-hide animation: none / slide / fade | none |

### 3.2 Per-child extra data (`splitter_item_full_v3` extra)

Format ID: fresh GUID `bettertab_extra_v1`. Same TLV shape as the settings section:

```text
uint16 version (1), then repeated (uint16 field_id, uint16 len, bytes):
  1 title (UTF-8)          2 title is title-format (bool)   3 hidden (bool)
  4 icon glyph (uint32 code point, 0 = none)                 5 icon font family (UTF-8, empty = default)
```

We also mirror title and hidden into `splitter_item_full_t::m_custom_title`/`set_title`/`m_hidden`,
so copying a child from our container to a CUI splitter (or back) keeps the title and hidden flag
even though the other side ignores our extra data (H13). Icon glyph default font: Segoe Fluent Icons
-> Segoe MDL2 Assets -> Segoe UI Symbol (the only one on Windows 7), chosen at run time.

## 4. Layering (keeps the DUI container possible)

```text
hosts/cui/     container, host, colours/font clients, FCL, configure dialog glue
  |
strip/         strip_model (tabs, active, hover, focus, pins), strip_layout (pure: metrics in,
               rects out), strip_renderer (D2D into a DIB), strip_window (HWND, input, TME),
               autohide (state machine, timers)
  |
model/         settings + blob codec, tab extra codec, title_format cache, cover_accent (ported)
  |
platform/      fb2k bridge, dpi (GetProcAddress), perf counters, logging, com_ptr, error (Result<T>)
```

`strip/` knows nothing about `uie::` or `cui::`: it takes a `Theme` value (colours, dark flag,
IDWriteTextFormat, accent) and emits intents (`activate(i)`, `reorder(from, to)`, `menu(i, pt)`).
A future `ui_element` host feeds it DUI colours/fonts and hosts DUI children instead.

C++ rules copied from foo_mediabar: C++23, `/W4 /WX`, angle-bracket SDK includes as external,
`std::expected` inside, `noexcept` + catch-all at every SDK/Win32 boundary, no raw `new`/`delete`
(except the owned `splitter_item_t*` the SDK demands), `/d2notypeopt`, `/MT`, frame pointers on.

## 5. Runtime design

### 5.1 Lazy children and switching

- **Activation** of tab `i` (click, key, wheel, follow-playback, `set_window_visibility`):
  1. If `i` has no window: `create_by_guid`, `set_config_from_ptr`, `create_or_transfer_window(us,
     host, content_rect)`. Log create time under the debug flag. Eager mode creates all at
     `WM_CREATE` instead, all hidden.
  2. One `BeginDeferWindowPos(2)`: old child `SWP_HIDEWINDOW|SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|
     SWP_NOACTIVATE`; new child `SWP_SHOWWINDOW|SWP_NOACTIVATE|SWP_NOZORDER` plus its rect **only if
     `size_stale`** (else `SWP_NOMOVE|SWP_NOSIZE`). `EndDeferWindowPos`.
  3. Strip: invalidate the old and new tab rects only (and the indicator path if animating).
  4. Focus: if focus was inside the old child, move it to the new child (`SetFocus`).
- **No `WM_SETREDRAW` by default.** It forces a full `RedrawWindow` of the container afterwards,
  which repaints more than the swap does. Implemented behind a hidden advconfig switch so M(a) can
  measure both; keep it only if the numbers say so.
- **Our resize:** only the active child and the strip are moved (one `DeferWindowPos`). Hidden children
  get `size_stale = true` and are sized when they become active. Nobody lays out an invisible panel.
- Container window: `WS_CLIPCHILDREN|WS_CLIPSIBLINGS`, `use_transparent_background = false`, no
  class brush, `WM_ERASEBKGND` returns 1. It paints only when it has no visible child (0 tabs, or
  the strip is "never" and there are no tabs): one `FillRect` in the CUI background colour.
- Children are never destroyed on switch. They are destroyed on `remove_panel`, `replace_panel`,
  `destroy_window`, and (only in `set_window_visibility`/`relinquish_ownership` paths) exactly as
  Tab stack does.
- 0 children: strip shows nothing (or a subtle "No panels" hint in the Layout preview only). 1 child
  with "2+ tabs": strip hidden, content gets everything. 30 children: overflow (5.3).

### 5.2 Paint pipeline (strip only)

- The strip is its **own child window**, a sibling of the content children. Invalidating it never
  touches a child. `WS_CLIPSIBLINGS` on the strip and on the container's children list.
- Back buffer: one persistent 32 bpp top-down DIB section the size of the strip, recreated only on
  resize/DPI change. An `ID2D1DCRenderTarget` (software, premultiplied) is bound to it once per paint
  with `BindDC(memdc, &dirty)`. D2D/DirectWrite rather than GDI because we need antialiased rounded
  corners and pill shapes, DirectWrite pixel-snapped text (drawing-overlays.md), and the same bitmap
  must feed `UpdateLayeredWindow` in overlay mode. HwndRenderTarget was rejected: it presents the
  whole target, not the dirty rect.
- `WM_PAINT`: `BeginPaint` -> clip to `ps.rcPaint` (`PushAxisAlignedClip`) -> draw background, tabs
  that intersect, indicator, focus ring, chevron -> `EndDraw` -> `BitBlt` **only `rcPaint`** from the
  DIB -> `EndPaint`. In layered mode, `UpdateLayeredWindowIndirect` with `prcDirty`.
- Caches (built outside `WM_PAINT`): one `IDWriteTextLayout` per tab, its metrics and snapped origin,
  tab rects, brushes (keyed to target generation, as foo_mediabar does). Invalidated by: font change,
  DPI change, title change, strip size change, settings change. Paint only reads them.
- No allocations in `WM_PAINT`: checked with foo_mediabar's module-local allocation counter under the
  debug flag, per paint, reported as "allocating paints".
- Text: family/size from `cui::fonts::get_font(id)->create_text_format()`, falling back to
  `get_log_font_with_fallback` + `IDWriteGdiInterop`. `rendering_options()` honoured. Origin snapped
  to device pixels. ClearType only when the pixels under the text are opaque, i.e. not in overlay
  mode with a translucent strip (drawing-overlays.md); greyscale otherwise.
- Rotated side text: a 90/270 degree transform, snapped in the rotated space. Measured with the
  same cached layouts (width and height swapped).
- Colours: `cui::colours::helper(our client GUID)`: background, text, selection text/background,
  hover = text over background at 8% (dark) / 6% (light). Accent: selection background, custom, or
  cover (5.6), passed through foo_mediabar's `accent_for_background` for contrast.

### 5.3 Layout, overflow, input

- `strip_layout` is a pure function: (tabs' measured sizes, settings in DIP, DPI, strip rect) ->
  tab rects, visible range, chevron rect. Fit / equal (max width) / fill (distribute the rest).
  Alignment applies when there is slack. Recomputed only when an input changes.
- Overflow: tabs that don't fit are not drawn; a chevron at the end opens a `TrackPopupMenu` with
  every tab, the active one checked. Dark mode: CUI sets the process's preferred app mode, so
  native menus follow it. **To verify at M(b)**; fallback is an owner-drawn menu with our colours.
  The active tab is always scrolled into the visible range.
- Hit-testing: binary search on the sorted tab edges. No loops over windows.
- Hover: `TrackMouseEvent(TME_LEAVE)` armed on the first `WM_MOUSEMOVE`; hover index change
  invalidates two tab rects. No `TME_HOVER`, no timers.
- Wheel over the strip: next/previous visible tab (`WHEEL_DELTA` accumulated, screen coords ->
  client, wtl-quirks). Middle click: per setting. Right click: our strip menu (6.3).
- Drag reorder: `SetCapture` after a 4 px (DIP, `SM_CXDRAG`) threshold; tabs animate (or snap) to the
  insertion point; on drop, build `order[new] = old` and call **our own `reorder_panels`**, so
  the stored order, the live windows and the Layout tree all see one change. Esc cancels.
- Keyboard:
  - Strip focused: Left/Right (Up/Down for side strips), Home/End, Enter/Space activates, Tab via H6,
    Apps key / Shift+F10 opens the menu at the focused tab.
  - **Ctrl+Tab / Ctrl+Shift+Tab while focus is inside a child**: children get the keystrokes, not us,
    and we must not install hooks. We register **one** `message_filter` (fb2k's pretranslate chain,
    `message_loop_v2::add_message_filter_ex(f, WM_KEYDOWN, WM_KEYDOWN)`, so it isn't even called for
    other messages) while at least one instance exists. It checks `VK_TAB` + Ctrl, walks up from
    `msg.hwnd` to the nearest bettertab container (a class-name compare per ancestor, only on
    Ctrl+Tab), and switches there. Nested containers: the innermost wins. Not executed in modal loops
    (`message_loop.h:6`), which is correct. Open question Q3.
- Focus ring only after keyboard use (`WM_UPDATEUISTATE`/`UISF_HIDEFOCUS`).

### 5.4 Auto-hide

State machine in `strip/autohide`, per instance:

```text
Hidden --enter hot zone--> RevealPending --reveal delay--> Showing(anim) -> Shown
Shown --leave strip & hot zone, no pins--> HidePending --hide delay--> Hiding(anim) -> Hidden
any pending --pointer back--> previous stable state (timer killed)
pins (keep Shown): menu open, drag in progress, strip has focus, linger after a switch
```

- One delay timer (reveal, hide or linger - only one can be pending) and one animation timer while
  an animation runs. Nothing else. Animations off: state changes are one `SetWindowPos` /
  `ShowWindow`, no timer.
- **Hot zone**: a thin input-only child window along the strip's edge (4 DIP default), topmost in
  the container z-order. `TrackMouseEvent` on it and on the strip. While the strip is shown the hot
  zone is hidden (the strip covers it), and "left both" becomes "left the strip".
  - Trade-off: the hot zone takes those 4 px of mouse input from the child under it (e.g. a scroll
    bar's last pixels on a right-hand strip). A click in the hot zone reveals immediately.
- **Overlay (default)** must not make the child repaint when the strip hides:
  - Windows 8+: strip and hot zone are **`WS_EX_LAYERED` child windows** (child layered windows
    are Windows 8+). DWM composes them over the child; showing/hiding does not invalidate the
    child. Strip content goes through `UpdateLayeredWindowIndirect` from the same DIB; hot zone is
    alpha 1 (invisible, still hit-testable).
  - Windows 7: child layered windows don't exist. Fallback: the strip is an owned, non-activating
    `WS_POPUP | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE` window positioned over the
    strip area at reveal time (hidden on main-window move/minimise/deactivate). With DWM on, hiding
    a layered top-level window doesn't invalidate what's under it. With DWM off (Basic theme), it
    does; accepted and documented. The hot zone on Win7 is a transparent (`WS_EX_TRANSPARENT`,
    non-painting) child.
  - **Measured, not assumed** (section 7): after each hide, `GetUpdateRect(child)` must be empty, and
    under the debug flag a temporary `SetWindowSubclass` counts the child's `WM_PAINT` /
    `WM_ERASEBKGND` (Win7 and Win10/11 both reported).
- **Push**: the content rect shrinks while shown; the child is resized (expected and documented as
  the slower mode).
- Hover never switches tabs; click does. Keyboard focus on the strip (Ctrl+Tab doesn't focus it)
  pins it. After a switch the strip lingers (700 ms default) so the choice is visible.
- Slide = the strip's position animates from outside the edge (layered: `UpdateLayeredWindow`
  position only, no re-render); fade = `SourceConstantAlpha`. Both 150 ms ease-out, cancellable.
  Push mode with slide animates the content rect too, so push + slide is marked "slow" in the UI.

### 5.5 Title formatting

- Custom title can be a title-format string (flag in extra data). Compiled once per string change
  with `titleformat_compiler`, cached per tab. Evaluated with `playback_control::playback_format_title`
  on `on_playback_new_track`, `on_playback_stop`, `on_playback_dynamic_info_track` - **only if** some
  tab has a script, and only if its output changed does that tab re-measure and invalidate. No
  per-second refresh (documented: `%playback_time%` is not live).
- Plain title: custom title, else the child's `get_name()` (or `get_short_name()` if it has one).

### 5.6 Cover accent

- Only when accent source = cover and the instance is visible. Shared `play_hub` subscribes to
  `now_playing_album_art_notify_manager` (B3) only while some instance uses it.
- Decode off the main thread (`fb2k::splitTask`, WIC, downscale to 256x256), `extract_cover_accent`
  from foo_mediabar (copied with attribution in the file header, plus its `accent_test`), result back
  with `inMainThread`, cached by bytes (size + `memcmp` vs last). One colour per cover; the strip
  repaints only the indicator/active tab. No cover: fall back to the CUI selection colour.

### 5.7 DPI

- All settings in DIP; `dpi = GetDpiForWindow` (resolved at run time) else `LOGPIXELSX`.
- `WM_DPICHANGED` goes to top-level windows only; a child learns of a per-monitor move via
  `WM_DPICHANGED_AFTERPARENT` (Win10 1703+) or, generally, the resize CUI does after the move. The
  strip re-reads DPI on `WM_DPICHANGED_AFTERPARENT` and on every `WM_SIZE`/`WM_WINDOWPOSCHANGED`
  (one cheap call), and rebuilds metrics/layouts only when it differs.

## 6. Appearance and UI

### 6.1 Strip look

Flat. Background = CUI background (or 3% lifted in dark mode, so the strip reads as chrome).
Text at 70% for inactive tabs, 100% for active and hover. Indicator: underline bar (2 DIP, rounded
ends, under the text width + padding), pill (full tab height minus 4 DIP, corner radius setting),
or none (active tab text only). Chip look: every tab gets a faint rounded background, active gets
the accent at 18%. No close buttons in v1 ("close-free" chips).

### 6.2 Configure dialog

`references/preferences-pages.md` applies in full: child dialog per tab (Strip, Tabs, Behaviour,
Auto-hide), 300 x 246 DU pages, one style profile comment in the `.rc`, `CDarkModeHooks` on every
child, edit margins helper, guarded `WM_NOTIFY`, `dialog_check.bat` = 0 problems after every layout
change. It is a modal dialog owned by the main window (not a Preferences page - settings are per
instance). "Live preview": edits apply to the **real** instance immediately (the strip re-renders;
the blob is not touched until OK); Cancel restores the snapshot taken on open. The Tabs page has the
per-tab list: title, title-format flag, icon glyph picker (code point + preview), hidden, move
up/down (-> `reorder_panels`).

### 6.3 Menus

Strip right-click: tab list (radio), then Rename..., Hide tab, Move left/right, Configure...,
and the child's own `uie::menu_window` items when it has them (as a submenu). CUI's own layout
editing menu is not replaced. We don't derive from `uie::menu_window` in v1 (it would only merge
our items into CUI's live-edit menu); revisit if you want that.

## 7. Measurement (debug flag = advconfig "Better tab: log performance")

All via QPC, printed to the console, zero cost when off (one bool check):

| Metric | How |
| --- | --- |
| Switch latency | activation start -> `EndDeferWindowPos` returned; separately -> first `WM_PAINT` of the new child, observed by an immediate `UpdateWindow(child)` under the flag only |
| Artwork view first switch | as above, first activation (includes `create_or_transfer_window`); also time to first paint |
| Strip paint | `BeginPaint`..`EndPaint`, p50/p95/max, dirty-area pixels, allocations per paint (must be 0) |
| Idle | timers armed (must be 0 when idle), paints per minute while idle (must be 0) |
| Overlay hide | `GetUpdateRect(child)` after hide (must be empty); child `WM_PAINT` count via temporary subclass |
| Layout pass | strip layout + text layout rebuild time on font/DPI change |

Report table for 20 tabs (Artwork view, ESLyric, Item properties, Album list, the rest spectrum/text
panels): switch p50/p95, first-switch Artwork view latency and time to its first paint, strip paint
p50/p95, allocations. I can't run foobar2000, so you run the checklist and paste the console block;
the numbers go into README "Performance" and this file.

## 8. Repo, build, release

- Own git repo, `.gitattributes` pinning line endings (first commit after the plan, like
  foo_mediabar). Layout: `build.bat`, `package.bat`, `foo_bettertab.vcxproj`, `.rc`, `resource.h`,
  `src/...`, `test/`, `docs/images/`, `README.md`, `PLAN.md`, `TASK.md` (handoff state), `PROMPT.md`
  (your brief, verbatim).
- `build.bat [Release|Debug] [x64|Win32]` and `package.bat [x64|both]` copied from foo_mediabar;
  `package.bat both` is the default for releases here since x86 is required. Plus a `dumpbin -imports`
  check for post-Win7 imports that fails the build.
- Version `0.1.0` -> milestones bump minor; `src/version.h`, one `DECLARE_COMPONENT_VERSION`,
  `VALIDATE_COMPONENT_FILENAME("foo_bettertab.dll")`. PDBs archived in `dist/symbols`.
- Commit style: `M<n>: summary` or `<version>: summary`, then a short bullet body (foo_mediabar).
- Offline tests in `test/`: blob codec (round-trip, truncated, unknown section, newer version,
  downgrade), extra-data codec, strip layout (fit/equal/fill x 4 positions x 0/1/30 tabs x 3 DPIs),
  accent (ported test), and a render test drawing the strip to a PNG at 100/150/200% for the
  README and for pixel checks (snapped text, crisp 1 px edges).

## 9. Milestones

Each ends with both architectures built, `dumpbin` clean, a test checklist, and a commit.

| M | Content | Version |
| --- | --- | --- |
| a | Container (`splitter_window_v3`, host, all H-rules), plain GDI-free strip (text only, D2D pipeline already), lazy/eager children, switching, blob + extra data + FCL, reorder, debug counters | 0.1.0 |
| b | Colour + font clients, dark mode live, indicator styles, accent (selection/custom/cover), rounded/chip, overflow chevron + menu, DPI | 0.2.0 |
| c | Configure dialog (4 tabs, live preview), all options, per-tab titles/icons/hidden, title format, mouse/keyboard incl. Ctrl+Tab filter, follow playback, remember tab | 0.3.0 |
| d | Auto-hide (hot zone, overlay Win8+/Win7 fallback, push, pins, delays), animations (indicator slide, strip cross-fade, show/hide slide/fade) | 0.4.0 |
| - | README with screenshots (foo_osd style), `.fb2k-component`, skill updates | 1.0.0 |

Test matrix (every milestone where it applies): two instances side by side; better tab nested in
better tab; Artwork view, ESLyric, Item properties, Album list as children; layout switch; FCL
export/import round-trip; downgrade (load a blob from M(n+1) in M(n)); dark/light toggle live; DPI
change live and per-monitor move; 0, 1 and 30 children.

## 10. Skill updates already found

To add once verified in a build (AGENTS.md: verify before writing):

- `columns-ui-sdk/references/sdk-quirks.md`: `reorder_panels` order semantics are `new[i] =
  old[order[i]]` in CUI's own Tab stack (`splitter_tabs.cpp:852-858`), not what the header wording
  suggests.
- `columns-ui-sdk/references/panels-and-toolbars.md`: Tab stack's host behaviour as a reference
  implementation (visibility modifiable only towards visible, `request_resize` false, size limits
  over created children, constant host GUID).
- `container_window_v3` forwards `WM_SETTINGCHANGE` to direct children, so a panel hosted in a CUI
  splitter **does** get it; the `drawing-overlays.md` line "a panel never gets WM_SETTINGCHANGE" is
  only true for DUI (to check in the host and correct in place).
- Win32 facts to confirm and record: child `WS_EX_LAYERED` is Windows 8+; whether hiding it
  invalidates the sibling below.

## 11. Open decisions (need your answer before M(a))

- **Q1 Renderer.** D2D `DCRenderTarget` into one DIB (plan above) vs plain GDI. GDI is simpler and
  smaller but gives jagged rounded corners and pills. I recommend D2D.
- **Q2 Win7 overlay.** Owned layered popup fallback (plan) vs plain child strip on Win7 (simpler,
  but the child repaints the covered strip area on hide). I recommend the popup, Win7 only.
- **Q3 Ctrl+Tab** from inside a child needs fb2k's `message_filter` (not a Windows hook, filtered to
  `WM_KEYDOWN` only). OK, or restrict Ctrl+Tab to when the strip has focus?
- **Q4 `request_resize`**: forward for the active child (plan) or always `false` like Tab stack?
- **Q5 Name** in the Layout tree: "Better tab" (category Splitters/Containers)? And should the
  README/package say it *replaces* Tab stack, or can both coexist (they can, technically)?
- **Q6 x86 target version**: keep `FOOBAR2000_TARGET_VERSION 81` for both (v2 only), or 80 for x86
  so it also loads in v1.6?
