// The host-agnostic part of a Better Tabs container (tabs_core.h). Moved out of the Columns UI
// container in 0.5.0 so the Default UI element shares every line of it.
//
// Behaviour follows Columns UI's own Tab stack wherever the SDK leaves room for choice
// (foo_ui_columns/splitter_tabs.cpp): missing panels, picking a neighbour, the shown child.

#include "tabs_core.h"

#include <commdlg.h>
#include <uxtheme.h>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "uxtheme.lib")

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fbc/fonts.h"

#include "../model/colour.h"
#include "../model/font_size.h"
#include "../platform/graphics.h"
#include "../platform/logging.h"
#include "../platform/perf.h"
#include "host_util.h"

namespace bettertabs {

namespace {

//! Every live container. Main thread.
std::vector<TabsCore*>& live_list() {
    static std::vector<TabsCore*> list;
    return list;
}

constexpr unsigned menu_tab_base = 1;
//! Commands on the clicked tab, then one id per hidden tab (tabs_ index).
constexpr unsigned menu_cmd_base = 3000;
enum TabCommand : unsigned {
    cmd_rename = menu_cmd_base,
    cmd_hide,
    cmd_move_back,
    cmd_move_forward,
    cmd_configure,
    cmd_hide_selected,
    cmd_clear_selection,
};
constexpr unsigned menu_unhide_base = 3100;
//! The Appearance submenu (until the Configure dialog in M(c), and kept as quick access after).
constexpr unsigned menu_style_base = 5000;
constexpr unsigned menu_panel_base = 10000;

enum StyleCommand : unsigned {
    style_position_top = menu_style_base,
    style_position_bottom,
    style_position_left,
    style_position_right,
    style_rotate_side_text,
    style_indicator_underline,
    style_indicator_pill,
    style_indicator_none,
    style_indicator_tab,
    style_indicator_tab_outline,
    style_chip,
    style_accent_selection,
    style_accent_highlight,
    style_accent_cover,
    style_accent_custom,
    style_sizing_fit,
    style_sizing_equal,
    style_sizing_fill,
    style_align_start,
    style_align_centre,
    style_align_end,
    style_chevron_start,
    style_shrink_titles,
    style_show_always,
    style_show_two_or_more,
    style_show_auto_hide,
    style_strength_auto,
    style_strength_subtle,
    style_strength_medium,
    style_strength_strong,
    style_strength_solid,
    style_background_theme,
    style_background_tint,
    style_background_custom,
    style_last,
};

// Auto-hide timers: the component's only timers, and only while something is due.
constexpr UINT_PTR timer_ah_delay = 0xB701;
constexpr UINT_PTR timer_ah_frame = 0xB702;

//! One DeferWindowPos batch that degrades to plain SetWindowPos calls if the batch fails.
class WindowMoves {
public:
    void add(HWND wnd, const RECT& rc, UINT flags) noexcept {
        if (wnd == nullptr || count_ == moves_.size()) return;
        moves_[count_++] = Move{wnd, rc, flags | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER};
    }
    void apply() noexcept {
        if (count_ == 0) return;
        HDWP dwp = BeginDeferWindowPos(static_cast<int>(count_));
        for (std::size_t i = 0; i < count_ && dwp != nullptr; ++i) {
            const Move& m = moves_[i];
            dwp = DeferWindowPos(dwp, m.wnd, nullptr, m.rc.left, m.rc.top, m.rc.right - m.rc.left,
                                 m.rc.bottom - m.rc.top, m.flags);
        }
        if (dwp != nullptr && EndDeferWindowPos(dwp)) {
            count_ = 0;
            return;
        }
        for (std::size_t i = 0; i < count_; ++i) {
            const Move& m = moves_[i];
            SetWindowPos(m.wnd, nullptr, m.rc.left, m.rc.top, m.rc.right - m.rc.left, m.rc.bottom - m.rc.top,
                         m.flags);
        }
        count_ = 0;
    }

private:
    struct Move {
        HWND wnd{nullptr};
        RECT rc{};
        UINT flags{0};
    };
    std::array<Move, 4> moves_{};
    std::size_t count_{0};
};

//! The system colour picker, seeded with and writing back 0xAARRGGBB. False if cancelled.
bool pick_colour(std::uint32_t& argb) noexcept {
    static COLORREF custom_colours[16]{};
    CHOOSECOLORW cc{sizeof(cc)};
    cc.hwndOwner = core_api::get_main_window();
    cc.rgbResult = colour::colorref_from_rgb(argb & 0xFFFFFFu);
    cc.lpCustColors = custom_colours;
    cc.Flags = CC_RGBINIT | CC_FULLOPEN;
    if (!ChooseColorW(&cc)) return false;
    argb = 0xFF000000u | colour::rgb_from_colorref(cc.rgbResult);
    return true;
}


// ---------------------------------------------------------------------------------------------
// Process-wide helpers, alive while at least one container has a window: one playback callback
// and one keyboard filter however many containers there are.

void broadcast(PlaybackEvent event) noexcept {
    // A copy: following playback creates panel windows, which could add or remove containers.
    const std::vector<TabsCore*> list = TabsCore::live();
    for (TabsCore* container : list) {
        if (std::find(TabsCore::live().begin(), TabsCore::live().end(), container) != TabsCore::live().end()) {
            container->on_playback(event);
        }
    }
}

//! Never from inside the play callback itself: following playback can create a panel, and a
//! panel that registers its own play callback while the callbacks are being dispatched crashed
//! foobar2000 (Artwork view, Item properties).
void post(PlaybackEvent event) noexcept {
    try {
        fb2k::inMainThread([event] { broadcast(event); });
    } catch (...) {
    }
}

class PlaybackWatch : public play_callback_impl_base {
public:
    PlaybackWatch()
        : play_callback_impl_base(flag_on_playback_starting | flag_on_playback_new_track | flag_on_playback_stop |
                                  flag_on_playback_pause | flag_on_playback_edited |
                                  flag_on_playback_dynamic_info_track) {}

    //! Playback the user started: play, next, previous, a double-clicked track. Not the next
    //! track of a playlist, and not the session resumed when foobar2000 starts.
    void on_playback_starting(play_control::t_track_command command, bool) override {
        if (command != play_control::track_command_resume) post(PlaybackEvent::started);
    }
    void on_playback_new_track(metadb_handle_ptr) override { post(PlaybackEvent::title); }
    void on_playback_stop(play_control::t_stop_reason reason) override {
        if (reason == play_control::stop_reason_starting_another) return; // a new track follows
        // Not while foobar2000 shuts down: that would create panels only to destroy them.
        if (reason != play_control::stop_reason_shutting_down) post(PlaybackEvent::stopped);
    }
    void on_playback_pause(bool) override { post(PlaybackEvent::title); }
    void on_playback_edited(metadb_handle_ptr) override { post(PlaybackEvent::title); }
    void on_playback_dynamic_info_track(const file_info&) override { post(PlaybackEvent::title); }
};

//! Ctrl+Tab and Ctrl+Shift+Tab anywhere inside a container. The innermost container that takes
//! it wins, so nested containers each cycle their own tabs.
class CtrlTabFilter : public message_filter_impl_base {
public:
    CtrlTabFilter() : message_filter_impl_base(WM_KEYDOWN, WM_KEYDOWN) {}

    bool pretranslate_message(MSG* msg) override {
        if (msg == nullptr || msg->message != WM_KEYDOWN || msg->wParam != VK_TAB) return false;
        if (GetKeyState(VK_CONTROL) >= 0 || GetKeyState(VK_MENU) < 0) return false;
        const int direction = GetKeyState(VK_SHIFT) < 0 ? -1 : 1;
        for (HWND wnd = msg->hwnd; wnd != nullptr; wnd = GetAncestor(wnd, GA_PARENT)) {
            for (TabsCore* container : TabsCore::live()) {
                if (container->core_wnd() == wnd) {
                    if (container->cycle_tabs(direction)) return true;
                    break;
                }
            }
            if ((GetWindowLongPtrW(wnd, GWL_STYLE) & WS_CHILD) == 0) break;
        }
        return false;
    }
};

struct Shared {
    unsigned windows{0};
    std::unique_ptr<PlaybackWatch> playback;
    std::unique_ptr<CtrlTabFilter> keys;
};

Shared& shared() {
    static Shared instance;
    return instance;
}

void attach_shared() noexcept {
    Shared& s = shared();
    if (s.windows++ != 0) return;
    try {
        s.playback = std::make_unique<PlaybackWatch>();
        s.keys = std::make_unique<CtrlTabFilter>();
    } catch (const std::exception& e) {
        log::warn(std::string("could not watch playback or keys: ") + e.what());
    }
}

void detach_shared() noexcept {
    Shared& s = shared();
    if (s.windows == 0 || --s.windows != 0) return;
    s.playback.reset();
    s.keys.reset();
}

} // namespace

const std::vector<TabsCore*>& TabsCore::live() noexcept { return live_list(); }

TabsCore::TabsCore() { live_list().push_back(this); }

TabsCore::~TabsCore() {
    close_configure_dialog(std::exchange(configure_wnd_, nullptr));
    std::erase(live_list(), this);
    ah_sync_parent_watch();
}

// ---------------------------------------------------------------------------------------------
// Configuration.

void TabsCore::load(InstanceData&& data) {
    settings_ = data.settings;
    unknown_settings_ = std::move(data.unknown_settings);
    unknown_sections_ = std::move(data.unknown_sections);
    saved_active_ = data.active;
    tabs_.clear();
    tabs_.reserve(data.children.size());
    for (ChildRecord& child : data.children) {
        auto tab = host_new_tab();
        tab->guid = child.guid;
        tab->config = std::move(child.config);
        tab->extra = decode_tab_extra(child.extra);
        tabs_.push_back(std::move(tab));
    }
}

InstanceData TabsCore::snapshot(bool refresh_children) const {
    InstanceData data;
    data.settings = settings_;
    data.unknown_settings = unknown_settings_;
    data.unknown_sections = unknown_sections_;
    const std::size_t active = index_of(active_);
    data.active = active != no_index ? static_cast<std::uint32_t>(active) : saved_active_;
    data.children.reserve(tabs_.size());
    for (const auto& tab : tabs_) {
        ChildRecord child;
        child.guid = tab->guid;
        child.config = tab->config;
        if (refresh_children && tab->wnd != nullptr) {
            try {
                child.config = host_child_config(*tab);
            } catch (const std::exception& e) {
                log::warn(std::string("could not read a panel's settings: ") + e.what());
            }
        }
        child.extra = encode_tab_extra(tab->extra);
        data.children.push_back(std::move(child));
    }
    return data;
}

void TabsCore::reload(InstanceData&& data) {
    const bool live = core_wnd() != nullptr;
    if (live) {
        for (auto& tab : tabs_) destroy_tab_window(*tab);
        active_ = nullptr;
    }
    load(std::move(data));
    if (!live) return;
    for (auto& tab : tabs_) host_prepare(*tab);
    strip_.set_settings(strip_settings());
    update_cover_subscription();
    refresh_colours();
    ah_update_mode();
    rebuild_strip();
    if (!settings_.lazy_children) {
        for (auto& tab : tabs_) {
            if (tab_visible(*tab)) ensure_window(*tab);
        }
    }
    ensure_active_valid();
    layout();
    limits_changed();
}

// ---------------------------------------------------------------------------------------------
// Children.

void TabsCore::host_query_limits(Tab& tab) noexcept {
    MINMAXINFO mmi{};
    mmi.ptMaxTrackSize.x = MAXLONG;
    mmi.ptMaxTrackSize.y = MAXLONG;
    SendMessageW(tab.wnd, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&mmi));
    tab.limits.min_width = static_cast<unsigned>(std::clamp(mmi.ptMinTrackSize.x, 0L, limit_cap));
    tab.limits.min_height = static_cast<unsigned>(std::clamp(mmi.ptMinTrackSize.y, 0L, limit_cap));
    tab.limits.max_width = static_cast<unsigned>(std::clamp(mmi.ptMaxTrackSize.x, 0L, limit_cap));
    tab.limits.max_height = static_cast<unsigned>(std::clamp(mmi.ptMaxTrackSize.y, 0L, limit_cap));
}

bool TabsCore::ensure_window(Tab& tab) noexcept {
    if (tab.wnd != nullptr) return true;
    const HWND self = core_wnd();
    if (self == nullptr || !host_prepare(tab)) return false;
    const std::uint64_t t_start = perf::enabled() ? perf::now() : 0;
    try {
        const HWND wnd = host_create_window(tab);
        if (t_start != 0) child_ms_ += perf::elapsed_ms(t_start, perf::now());
        if (wnd == nullptr) return false;
        if ((GetWindowLongPtrW(wnd, GWL_STYLE) & WS_VISIBLE) != 0) {
            // Same repair as Tab stack; a panel should be created hidden.
            ShowWindow(wnd, SW_HIDE);
        }
        SetWindowLongPtrW(wnd, GWL_STYLE, GetWindowLongPtrW(wnd, GWL_STYLE) | WS_CLIPSIBLINGS);
        tab.wnd = wnd;
        tab.applied = content_;
        host_query_limits(tab);
        update_label(tab);
        return true;
    } catch (const std::exception& e) {
        log::warn(std::string("could not create a panel window: ") + e.what());
        return false;
    }
}

void TabsCore::destroy_tab_window(Tab& tab) noexcept {
    host_destroy(tab);
    tab.wnd = nullptr;
    tab.applied = RECT{};
}

namespace {

//! No track: every field is handled but empty and not found, so it reads as "" instead of "?" and
//! $if() / [...] take their no-value branch. Functions are left to the core. titleformat_object::run
//! needs a real hook - the core calls p_source->process_field without a null check, so run(nullptr)
//! crashed foobar2000 on the first field (playback starting, before the track is open).
class NoTrackHook : public titleformat_hook {
public:
    bool process_field(titleformat_text_out*, const char*, t_size, bool& found) override {
        found = false;
        return true;
    }
    bool process_function(titleformat_text_out*, const char*, t_size, titleformat_hook_function_params*,
                          bool& found) override {
        found = false;
        return false;
    }
};

} // namespace

void TabsCore::update_label(Tab& tab) noexcept {
    try {
        if (tab.extra.use_custom_title && !tab.extra.title.empty()) {
            if (!tab.extra.title_is_format) {
                tab.script.release();
                tab.label = widen(tab.extra.title.c_str());
                return;
            }
            if (!tab.script.is_valid() || tab.script_source != tab.extra.title) {
                tab.script.release();
                titleformat_compiler::get()->compile_safe_ex(tab.script, tab.extra.title.c_str(), "(invalid title)");
                tab.script_source = tab.extra.title;
            }
            pfc::string8 text;
            // The playing track while there is one; otherwise the script runs without a track
            // (fields are empty, so $if() and the like can show something else).
            if (!playback_control::get()->playback_format_title(nullptr, text, tab.script, nullptr,
                                                                playback_control::display_level_all)) {
                NoTrackHook no_track;
                tab.script->run(&no_track, text, nullptr);
                // Nothing playing and the script gave nothing (a plain %title%): the panel's own
                // name rather than a blank tab. A script with its own fallback text keeps it.
                if (std::all_of(text.get_ptr(), text.get_ptr() + text.get_length(),
                                [](char c) { return c == ' ' || c == '\t'; })) {
                    tab.label = host_child_label(tab);
                    return;
                }
            }
            tab.label = widen(text.get_ptr());
            return;
        }
        tab.script.release();
        tab.label = host_child_label(tab);
    } catch (...) {
        tab.label.clear();
    }
}

void TabsCore::refresh_titles() noexcept {
    bool changed = false;
    for (auto& tab : tabs_) {
        if (!tab->script.is_valid()) continue;
        const std::wstring before = tab->label;
        update_label(*tab);
        changed = changed || tab->label != before;
    }
    if (changed && core_wnd() != nullptr) rebuild_strip();
}


bool TabsCore::tab_visible(const Tab& tab) const noexcept {
    return !tab.extra.hidden && !tab.pending_removal && host_present(tab);
}

Tab* TabsCore::find_by_wnd(HWND wnd) const noexcept {
    if (wnd == nullptr) return nullptr;
    for (const auto& tab : tabs_) {
        if (tab->wnd == wnd) return tab.get();
    }
    return nullptr;
}

std::size_t TabsCore::index_of(const Tab* tab) const noexcept {
    if (tab == nullptr) return no_index;
    for (std::size_t i = 0; i < tabs_.size(); ++i) {
        if (tabs_[i].get() == tab) return i;
    }
    return no_index;
}

std::size_t TabsCore::strip_index_of(const Tab* tab) const noexcept {
    const std::size_t index = index_of(tab);
    if (index == no_index) return no_index;
    for (std::size_t s = 0; s < visible_.size(); ++s) {
        if (visible_[s] == index) return s;
    }
    return no_index;
}

// ---------------------------------------------------------------------------------------------
// Tabs and switching.

void TabsCore::rebuild_strip() noexcept {
    try {
        visible_.clear();
        items_.clear();
        for (std::size_t i = 0; i < tabs_.size(); ++i) {
            const Tab& tab = *tabs_[i];
            if (!tab_visible(tab)) continue;
            visible_.push_back(i);
            StripItem item;
            item.icon = icon_text(tab.extra.icon);
            if (!item.icon.empty()) item.icon_font = widen(tab.extra.icon_font.c_str());
            if (!settings_.icons_only || item.icon.empty()) item.label = tab.label;
            item.tooltip = tab.label;
            items_.push_back(std::move(item));
        }
    } catch (...) {
        visible_.clear();
        items_.clear();
    }
    strip_.set_items(items_, strip_index_of(active_));
}

void TabsCore::set_tab_hidden(Tab* tab, bool hidden) noexcept {
    const std::size_t index = index_of(tab);
    if (index == no_index || tab->extra.hidden == hidden) return;
    // The last tab stays: an empty container could only be repaired from the layout.
    if (hidden && tab_visible(*tab) && visible_.size() < 2) return;
    tab->extra.hidden = hidden;
    if (core_wnd() == nullptr) return;
    rebuild_strip();
    if (hidden && tab == active_) {
        activate(fallback_after_removal(index), false);
    } else if (!hidden) {
        host_prepare(*tab);
        rebuild_strip();
        activate(tab, true);
    }
    ensure_active_valid();
    layout();
    limits_changed();
}

void TabsCore::move_tab(std::size_t from, std::size_t to) noexcept {
    if (from >= tabs_.size() || to >= tabs_.size() || from == to) return;
    std::unique_ptr<Tab> tab = std::move(tabs_[from]);
    tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(from));
    tabs_.insert(tabs_.begin() + static_cast<std::ptrdiff_t>(to), std::move(tab));
    if (core_wnd() != nullptr) rebuild_strip();
}

void TabsCore::rename_tab(Tab* tab) noexcept {
    if (index_of(tab) == no_index) return;
    const auto keep_alive = host_keep_alive();
    try {
        TabExtra extra = tab->extra;
        if (!run_rename_dialog(core_wnd(), host_panel_name(*tab), extra)) return;
        if (index_of(tab) == no_index) return; // the layout changed meanwhile
        tab->extra = extra;
        update_label(*tab);
        rebuild_strip();
    } catch (const std::exception& e) {
        log::warn(std::string("rename failed: ") + e.what());
    }
}

void TabsCore::on_playback(PlaybackEvent event) noexcept {
    if (core_wnd() == nullptr) return;
    if (event != PlaybackEvent::title) {
        for (const auto& tab : tabs_) {
            const bool wanted = event == PlaybackEvent::started ? tab->extra.show_on_play : tab->extra.show_on_stop;
            if (wanted && tab_visible(*tab)) {
                activate(tab.get(), false);
                break;
            }
        }
    }
    refresh_titles();
}

bool TabsCore::cycle_tabs(int direction) noexcept {
    if (!settings_.ctrl_tab || visible_.size() < 2) return false;
    const std::size_t count = visible_.size();
    const std::size_t current = strip_index_of(active_);
    std::size_t next = 0;
    if (current != no_index) next = direction > 0 ? (current + 1) % count : (current + count - 1) % count;
    activate(tabs_[visible_[next]].get(), true);
    return true;
}

Tab* TabsCore::fallback_after_removal(std::size_t removed_index) const noexcept {
    // The next visible tab, else the previous one (Tab stack picks the neighbour the same way).
    for (std::size_t i = removed_index; i < tabs_.size(); ++i) {
        if (tab_visible(*tabs_[i])) return tabs_[i].get();
    }
    for (std::size_t i = (std::min)(removed_index, tabs_.size()); i > 0; --i) {
        if (tab_visible(*tabs_[i - 1])) return tabs_[i - 1].get();
    }
    return nullptr;
}

void TabsCore::ensure_active_valid() noexcept {
    if (active_ != nullptr && index_of(active_) == no_index) active_ = nullptr; // defensive
    if (active_ != nullptr && tab_visible(*active_)) return;
    Tab* next = nullptr;
    if (settings_.remember_active && saved_active_ < tabs_.size() && tab_visible(*tabs_[saved_active_])) {
        next = tabs_[saved_active_].get();
    } else {
        next = fallback_after_removal(0);
    }
    // activate() also hides the old one when it was just hidden as a tab.
    activate(next, false);
}

void TabsCore::activate(Tab* next, bool from_user) noexcept {
    const HWND self = core_wnd();
    if (self == nullptr) {
        active_ = next;
        return;
    }
    if (next != nullptr && !tab_visible(*next)) next = nullptr;
    Tab* const old = active_;
    if (next == old && (next == nullptr || next->wnd != nullptr)) return;

    const bool measure = perf::enabled();
    const std::uint64_t t_start = measure ? perf::now() : 0;

    const HWND focus = GetFocus();
    const bool focus_in_old = old != nullptr && old->wnd != nullptr && focus != nullptr &&
                              (focus == old->wnd || IsChild(old->wnd, focus));

    const bool created = next != nullptr && next->wnd == nullptr;
    if (next != nullptr) ensure_window(*next);
    const std::uint64_t t_created = measure ? perf::now() : 0;

    const bool setredraw = perf::use_setredraw() && !in_create_;
    if (setredraw) SendMessageW(self, WM_SETREDRAW, FALSE, 0);

    // Size the new panel while it is still hidden, then show it, then hide the old one: the
    // parent's background is never exposed in between. Showing and hiding go through ShowWindow
    // (as Tab stack does), not SWP_SHOWWINDOW: (Defer)SetWindowPos never sends WM_SHOWWINDOW,
    // and Columns UI's splitters show their own children only from it, so a Row or Column tab
    // stayed empty (test/showwindow_test.cpp).
    active_ = next; // before showing: a child may ask the host about visibility meanwhile
    if (next != nullptr && next->wnd != nullptr) {
        const RECT rc = child_rect(*next);
        if (!same_rect(next->applied, rc)) {
            WindowMoves moves;
            moves.add(next->wnd, rc, 0);
            moves.apply();
        }
        next->applied = rc;
        if ((GetWindowLongPtrW(next->wnd, GWL_STYLE) & WS_VISIBLE) == 0) ShowWindow(next->wnd, SW_SHOWNA);
    }
    if (old != nullptr && old != next && old->wnd != nullptr) ShowWindow(old->wnd, SW_HIDE);
    if (old != nullptr && old != next && old->wnd != nullptr) host_child_shown(*old, false);
    if (next != nullptr && next->wnd != nullptr && (next != old || created)) host_child_shown(*next, true);

    if (setredraw) {
        SendMessageW(self, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(self, &content_, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
    }
    strip_.set_active(strip_index_of(next));
    // A newly created panel window starts on top of the z-order, above the auto-hide windows.
    if (created && auto_hide()) ah_raise();

    if (created) limits_changed();

    if (next != nullptr && next->wnd != nullptr && (focus_in_old || (from_user && focus == self))) {
        HWND target = next->wnd;
        if ((GetWindowLongPtrW(target, GWL_STYLE) & WS_TABSTOP) == 0) {
            target = GetNextDlgTabItem(next->wnd, next->wnd, FALSE);
        }
        const bool ok = target != nullptr && (target == next->wnd || IsChild(next->wnd, target)) &&
                        (GetWindowLongPtrW(target, GWL_STYLE) & WS_TABSTOP) != 0;
        // A hidden auto-hide strip must not take the focus: it would pin itself shown.
        SetFocus(ok ? target : (auto_hide() && !ah_shown_ ? next->wnd : strip_.hwnd()));
    }
    if (from_user) ah_note_switch();

    if (measure && next != nullptr && next->wnd != nullptr && !in_create_) {
        const std::uint64_t t_switched = perf::now();
        // Force the first paint now so it can be timed; with logging off it happens as usual.
        UpdateWindow(next->wnd);
        const std::uint64_t t_painted = perf::now();
        perf::PaintStats strip_stats;
        strip_.take_paint_stats(strip_stats);
        pfc::string_formatter f;
        f << "switch to \"" << narrow(next->label) << "\" (" << pfc::format_uint(tabs_.size()) << " tabs): "
          << pfc::format_float(perf::elapsed_ms(t_start, t_switched), 0, 3) << " ms";
        if (created) f << " incl. create " << pfc::format_float(perf::elapsed_ms(t_start, t_created), 0, 3) << " ms";
        f << ", to first paint " << pfc::format_float(perf::elapsed_ms(t_start, t_painted), 0, 3) << " ms";
        if (strip_stats.paints != 0) {
            f << "; strip: " << pfc::format_uint(strip_stats.paints) << " paints, worst "
              << pfc::format_float(strip_stats.worst_ms, 0, 3) << " ms";
            if (strip_stats.worst_ms >= 5.0) {
                // Which part, and whether it belongs to this switch at all: the stats cover every
                // paint since the last report, start-up included.
                if (strip_stats.worst_at != 0 && strip_stats.worst_at < t_start) {
                    f << " (" << pfc::format_float(perf::elapsed_ms(strip_stats.worst_at, t_start), 0, 1)
                      << " ms before the switch";
                } else {
                    f << " (during the switch";
                }
                f << "; bind " << pfc::format_float(strip_stats.worst_bind_ms, 0, 3) << ", draw "
                  << pfc::format_float(strip_stats.worst_draw_ms, 0, 3) << ", EndDraw "
                  << pfc::format_float(strip_stats.worst_flush_ms, 0, 3) << ", copy "
                  << pfc::format_float(strip_stats.worst_blit_ms, 0, 3) << ", "
                  << pfc::format_uint(strip_stats.worst_area) << " px" << (strip_stats.worst_switching ? ", animating" : "")
                  << ")";
            }
            f << ", " << pfc::format_uint(strip_stats.pixels) << " px, "
              << pfc::format_uint(strip_stats.allocating_paints) << " allocating";
        }
        if (setredraw) f << " [WM_SETREDRAW]";
        log::info(f.get_ptr());
    }
}

bool TabsCore::show_child(HWND wnd) noexcept {
    Tab* tab = find_by_wnd(wnd);
    if (tab == nullptr || !tab_visible(*tab)) return false;
    activate(tab, false);
    return active_ == tab;
}

// ---------------------------------------------------------------------------------------------
// Layout and size limits.

bool TabsCore::want_strip() const noexcept {
    switch (settings_.visibility) {
    case StripVisibility::never: return false;
    case StripVisibility::two_or_more: return visible_.size() >= 2;
    case StripVisibility::auto_hide: return ah_shown_ || ah_animating_;
    case StripVisibility::always:
    default: return true;
    }
}

void TabsCore::layout() noexcept {
    const HWND self = core_wnd();
    if (self == nullptr) return;
    RECT client{};
    GetClientRect(self, &client);
    const bool show = want_strip() && strip_.hwnd() != nullptr;
    const bool ah = auto_hide() && strip_.hwnd() != nullptr;
    RECT strip_rc{};
    content_ = client;
    if (show || ah) {
        const bool across_height =
            settings_.position == StripPosition::top || settings_.position == StripPosition::bottom;
        const int room = across_height ? client.bottom : client.right;
        const int t = (std::min)(strip_.thickness(), (std::max)(0, room));
        switch (settings_.position) {
        case StripPosition::top:
            strip_rc = RECT{0, 0, client.right, t};
            content_.top = t;
            break;
        case StripPosition::bottom:
            strip_rc = RECT{0, client.bottom - t, client.right, client.bottom};
            content_.bottom = client.bottom - t;
            break;
        case StripPosition::left:
            strip_rc = RECT{0, 0, t, client.bottom};
            content_.left = t;
            break;
        case StripPosition::right:
            strip_rc = RECT{client.right - t, 0, client.right, client.bottom};
            content_.right = client.right - t;
            break;
        }
    }

    // Auto-hide: over the panel the content keeps the whole client area and the strip slides or
    // fades as a layered child; pushing shrinks the content only while the strip is shown. The
    // hot zone lies along the edge while the strip is hidden, over the panel when it is layered,
    // else in room taken from the panel.
    RECT hot_rc{};
    bool hot_show = false;
    if (ah) {
        const bool overlay = ah_overlay();
        const int hz = (std::max)(1, ah_px(settings_.hot_zone));
        switch (settings_.position) {
        case StripPosition::top: hot_rc = RECT{0, 0, client.right, hz}; break;
        case StripPosition::bottom: hot_rc = RECT{0, client.bottom - hz, client.right, client.bottom}; break;
        case StripPosition::left: hot_rc = RECT{0, 0, hz, client.bottom}; break;
        case StripPosition::right: hot_rc = RECT{client.right - hz, 0, client.right, client.bottom}; break;
        }
        hot_show = hot_zone_.hwnd() != nullptr && !show;
        if (overlay || !ah_shown_) content_ = client;
        if (hot_show && !hot_zone_.layered()) {
            switch (settings_.position) {
            case StripPosition::top: content_.top = hot_rc.bottom; break;
            case StripPosition::bottom: content_.bottom = hot_rc.top; break;
            case StripPosition::left: content_.left = hot_rc.right; break;
            case StripPosition::right: content_.right = hot_rc.left; break;
            }
        }
        if (overlay && show && ah_animating_) {
            const float p = ah_progress_;
            if (settings_.show_hide_animation == ShowHideAnimation::slide) {
                const bool across_height =
                    settings_.position == StripPosition::top || settings_.position == StripPosition::bottom;
                const int t = across_height ? strip_rc.bottom - strip_rc.top : strip_rc.right - strip_rc.left;
                const int off = static_cast<int>(std::lround((1.0f - p) * static_cast<float>(t)));
                switch (settings_.position) {
                case StripPosition::top: OffsetRect(&strip_rc, 0, -off); break;
                case StripPosition::bottom: OffsetRect(&strip_rc, 0, off); break;
                case StripPosition::left: OffsetRect(&strip_rc, -off, 0); break;
                case StripPosition::right: OffsetRect(&strip_rc, off, 0); break;
                }
            } else {
                strip_.set_alpha(static_cast<BYTE>(std::lround(255.0f * std::clamp(p, 0.0f, 1.0f))));
            }
        } else if (strip_.layered()) {
            strip_.set_alpha(255);
        }
    }

    const bool strip_was_shown = strip_.hwnd() != nullptr && IsWindowVisible(strip_.hwnd());
    WindowMoves moves;
    if (strip_.hwnd() != nullptr) {
        if (show) {
            moves.add(strip_.hwnd(), strip_rc, SWP_SHOWWINDOW);
        } else if (strip_was_shown) {
            strip_.forget_pointer();
            moves.add(strip_.hwnd(), strip_rc, SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE);
        }
    }
    if (hot_zone_.hwnd() != nullptr) {
        if (hot_show) {
            moves.add(hot_zone_.hwnd(), hot_rc, SWP_SHOWWINDOW);
        } else if (IsWindowVisible(hot_zone_.hwnd())) {
            hot_zone_.forget_pointer();
            moves.add(hot_zone_.hwnd(), hot_rc, SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE);
        }
    }
    if (active_ != nullptr && active_->wnd != nullptr) {
        const RECT rc = child_rect(*active_);
        if (!same_rect(active_->applied, rc)) {
            active_->applied = rc;
            moves.add(active_->wnd, rc, 0);
        }
    }
    moves.apply();
    if (ah) ah_raise();

    if (ah && strip_was_shown && !show && perf::enabled() && active_ != nullptr && active_->wnd != nullptr) {
        // Hiding over the panel must not make the panel repaint. Measured here.
        RECT dirty{};
        const bool invalidated = GetUpdateRect(active_->wnd, &dirty, FALSE) != FALSE;
        pfc::string_formatter f;
        f << "auto-hide: strip hidden (" << (ah_overlay() ? "over the panel" : "push") << "), panel "
          << (invalidated ? "invalidated" : "not invalidated");
        log::info(f.get_ptr());
    }

    if (show != strip_shown_) {
        strip_shown_ = show;
        limits_changed();
    }
}

RECT TabsCore::child_rect(const Tab& tab) const noexcept {
    // Within the content area, never larger than the page allows (top left aligned); the rest
    // shows our background.
    RECT rc = content_;
    const auto cap = [](LONG& far_edge, LONG near_edge, unsigned max) {
        if (max < static_cast<unsigned>(limit_cap) && far_edge - near_edge > static_cast<LONG>(max)) {
            far_edge = near_edge + static_cast<LONG>(max);
        }
    };
    cap(rc.right, rc.left, tab.limits.max_width);
    cap(rc.bottom, rc.top, tab.limits.max_height);
    return rc;
}

Limits TabsCore::compute_limits() const noexcept {
    Limits out;
    out.min_width = 0;
    out.min_height = 0;
    out.max_width = limit_cap;
    out.max_height = limit_cap;
    // Every page's minimum, but only the largest page's maximum: the container is as flexible as
    // its most flexible page, and a page that cannot grow is shown at its maximum (child_rect).
    // Tab stack takes the smallest maximum instead, so one fixed-height page (an empty Playlist
    // tabs is just its tab row) squeezed the whole container to that height, on every tab.
    bool any = false;
    unsigned max_width = 0;
    unsigned max_height = 0;
    for (const auto& tab : tabs_) {
        if (tab->wnd == nullptr) continue;
        any = true;
        out.min_width = (std::max)(out.min_width, tab->limits.min_width);
        out.min_height = (std::max)(out.min_height, tab->limits.min_height);
        max_width = (std::max)(max_width, tab->limits.max_width);
        max_height = (std::max)(max_height, tab->limits.max_height);
    }
    if (any) {
        out.max_width = (std::max)(max_width, out.min_width);
        out.max_height = (std::max)(max_height, out.min_height);
    }
    // An auto-hidden strip never counts: showing it must not resize the layout around us.
    if (strip_shown_ && !auto_hide()) {
        const auto t = static_cast<unsigned>((std::max)(0, strip_.thickness()));
        const bool vertical_stack =
            settings_.position == StripPosition::top || settings_.position == StripPosition::bottom;
        unsigned& min_along = vertical_stack ? out.min_height : out.min_width;
        unsigned& max_along = vertical_stack ? out.max_height : out.max_width;
        min_along = (std::min)(min_along + t, static_cast<unsigned>(limit_cap));
        if (max_along < static_cast<unsigned>(limit_cap)) {
            max_along = (std::min)(max_along + t, static_cast<unsigned>(limit_cap));
        }
    }
    return out;
}

void TabsCore::limits_changed() noexcept {
    const Limits next = compute_limits();
    const bool same = next.min_width == limits_.min_width && next.min_height == limits_.min_height &&
                      next.max_width == limits_.max_width && next.max_height == limits_.max_height;
    limits_ = next;
    if (same || in_create_ || core_wnd() == nullptr) return;
    host_limits_changed();
}

void TabsCore::on_child_limits_changed(HWND wnd) noexcept {
    Tab* tab = find_by_wnd(wnd);
    if (tab == nullptr) return;
    host_query_limits(*tab);
    limits_changed();
    layout();
}

// ---------------------------------------------------------------------------------------------
// Window.

bool TabsCore::core_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) noexcept {
    result = 0;
    try {
        switch (msg) {
        case WM_CREATE: on_create(wnd); return true;
        case WM_DESTROY: on_destroy(); return true;
        case WM_SIZE: layout(); return true;
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = static_cast<LONG>(limits_.min_width);
            mmi->ptMinTrackSize.y = static_cast<LONG>(limits_.min_height);
            mmi->ptMaxTrackSize.x = static_cast<LONG>(limits_.max_width);
            mmi->ptMaxTrackSize.y = static_cast<LONG>(limits_.max_height);
            return true;
        }
        // Transparent children (Columns UI splitters, toolbars, many panels) paint their
        // background by forwarding these to us with their own DC and origin, so both must really
        // paint: an empty Row/Column splitter would otherwise keep whatever was on screen before.
        // Our own erase only reaches the area no child covers (WS_CLIPCHILDREN).
        // DrawThemeParentBackground (the transparent strip) sends both, WM_PRINTCLIENT with
        // PRF_CLIENT only: answering just the erase asks our parent once.
        case WM_ERASEBKGND: paint_background(reinterpret_cast<HDC>(wp));
            result = 1;
            return true;
        case WM_PRINTCLIENT:
            if ((lp & PRF_ERASEBKGND) != 0) paint_background(reinterpret_cast<HDC>(wp));
            return true;
        case WM_PAINT: {
            // Only reached where no child covers the client area (WS_CLIPCHILDREN), e.g. with no tabs.
            PAINTSTRUCT ps{};
            const HDC dc = BeginPaint(wnd, &ps);
            if (dc != nullptr) paint_background(dc);
            EndPaint(wnd, &ps);
            return true;
        }
        case WM_TIMER:
            if (wp == timer_ah_delay || wp == timer_ah_frame) {
                ah_on_timer(wp);
                return true;
            }
            break;
        case WM_SETCURSOR:
            // Fallback for a panel that raised itself by z-order alone (SetWindowPos(HWND_TOP)),
            // which raises no event at all: the pointer moving over it reaches us here, because
            // DefWindowProc asks the parent first. Never handled, so the cursor is unchanged.
            if (auto_hide() && reinterpret_cast<HWND>(wp) != wnd) ah_raise();
            break;
        case WM_SETFOCUS:
            if (active_ != nullptr && active_->wnd != nullptr) {
                SetFocus(active_->wnd);
            } else if (strip_.hwnd() != nullptr && strip_shown_) {
                SetFocus(strip_.hwnd());
            }
            return true;
        default: break;
        }
    } catch (const std::exception& e) {
        log::warn(std::string("container message failed: ") + e.what());
    } catch (...) {
        log::warn("container message failed");
    }
    return false;
}

void TabsCore::on_create(HWND wnd) noexcept {
    const bool measure = perf::enabled();
    const std::uint64_t t_start = measure ? perf::now() : 0;
    in_create_ = true;
    child_ms_ = 0.0;
    // Per-step timings for the log line (startup investigation, 0.3.1). Panel creation inside a
    // step is excluded from it, so each number is this component's own time.
    constexpr std::size_t step_count = 8;
    static constexpr const char* step_names[step_count] = {"shared", "strip", "cover",  "colours",
                                                           "font",   "objects", "layout", "activate"};
    double steps[step_count]{};
    std::uint64_t t_step = t_start;
    double child_before = 0.0;
    const auto step = [&](std::size_t index) {
        if (!measure) return;
        const std::uint64_t t = perf::now();
        steps[index] = (std::max)(0.0, perf::elapsed_ms(t_step, t) - (child_ms_ - child_before));
        child_before = child_ms_;
        t_step = t;
    };
    attach_shared();
    host_on_create();
    step(0);
    try {
        if (!strip_.create(wnd, *this)) log::warn("could not create the tab strip");
        strip_.set_settings(strip_settings());
        step(1);
        update_cover_subscription();
        step(2);
        refresh_colours();
        step(3);
        refresh_font();
        ah_update_mode();
        step(4);
        GetClientRect(wnd, &content_);
        for (auto& tab : tabs_) host_prepare(*tab);
        step(5);
        rebuild_strip();
        layout();
        step(6);
        if (!settings_.lazy_children) {
            for (auto& tab : tabs_) {
                if (tab_visible(*tab)) ensure_window(*tab);
            }
        }
        ensure_active_valid();
        layout();
        step(7);
    } catch (const std::exception& e) {
        log::warn(std::string("could not set up the container: ") + e.what());
    }
    in_create_ = false;
    limits_ = compute_limits();

    if (measure) {
        std::size_t created = 0;
        for (const auto& tab : tabs_) created += tab->wnd != nullptr ? 1 : 0;
        const double total = perf::elapsed_ms(t_start, perf::now());
        pfc::string_formatter f;
        f << "container created with " << pfc::format_uint(tabs_.size()) << " tabs (" << pfc::format_uint(created)
          << " panel windows) in " << pfc::format_float(total, 0, 3) << " ms: own "
          << pfc::format_float((std::max)(0.0, total - child_ms_), 0, 3) << " ms, panels "
          << pfc::format_float(child_ms_, 0, 3) << " ms (";
        for (std::size_t i = 0; i < step_count; ++i) {
            if (i != 0) f << ", ";
            f << step_names[i] << " " << pfc::format_float(steps[i], 0, 3);
            if (i == 1) {
                const StripWindow::CreateTimings& st = strip_.create_timings();
                f << " [class " << pfc::format_float(st.class_ms, 0, 3) << ", window "
                  << pfc::format_float(st.window_ms, 0, 3) << " (to WM_NCCREATE "
                  << pfc::format_float(st.to_nccreate_ms, 0, 3) << ", to WM_CREATE "
                  << pfc::format_float(st.to_create_ms, 0, 3) << ", after "
                  << pfc::format_float(st.after_create_ms, 0, 3) << "), state "
                  << pfc::format_float(st.state_ms, 0, 3) << ", text " << pfc::format_float(st.text_ms, 0, 3) << "]";
            }
        }
        char warm[32]{};
        gfx::warm_text_status(warm, sizeof(warm));
        f << "; text warm-up " << warm << ")";
        log::info(f.get_ptr());
    }
}

void TabsCore::on_destroy() noexcept {
    const std::size_t active = index_of(active_);
    if (active != no_index) saved_active_ = static_cast<std::uint32_t>(active);
    for (auto& tab : tabs_) destroy_tab_window(*tab);
    active_ = nullptr;
    KillTimer(core_wnd(), timer_ah_delay);
    KillTimer(core_wnd(), timer_ah_frame);
    ah_timer_ = AhTimer::none;
    ah_animating_ = false;
    ah_shown_ = false;
    ah_progress_ = 0.0f;
    menu_pin_ = false;
    if (configure_wnd_ != nullptr) {
        // The container goes first: what the dialog previewed is not kept.
        settings_ = configure_original_.settings;
        close_configure_dialog(std::exchange(configure_wnd_, nullptr));
        config_tabs_.clear();
    }
    hot_zone_.destroy();
    ah_sync_parent_watch();
    strip_.destroy();
    strip_shown_ = false;
    if (cover_subscribed_) {
        cover::unsubscribe(this);
        cover_subscribed_ = false;
    }
    visible_.clear();
    detach_shared();
    host_on_destroy();
}

void TabsCore::fill_background(HDC dc) const noexcept {
    if (dc == nullptr) return;
    RECT clip{};
    if (GetClipBox(dc, &clip) == ERROR || IsRectEmpty(&clip)) return;
    // A child's DC (or its buffer) means a transparent child asking for what lies behind it: the
    // host's layout background, so a splitter's dividers show as they do outside the container.
    const COLORREF fill = WindowFromDC(dc) == core_wnd() ? background_ : child_background_;
    // The stock DC brush: no GDI object is created per erase.
    const COLORREF previous = SetDCBrushColor(dc, fill);
    FillRect(dc, &clip, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SetDCBrushColor(dc, previous);
}

void TabsCore::paint_background(HDC dc) const noexcept {
    fill_background(dc);
    // Transparent: the request goes on to our parent, with the DC moved to its client area, so
    // the strip (and transparent children) show the layout's background (a Columns UI theme's
    // image). The fill above stays where the parent paints nothing.
    if (settings_.transparent_background && dc != nullptr && core_wnd() != nullptr) {
        DrawThemeParentBackground(core_wnd(), dc, nullptr);
    }
}

void TabsCore::refresh_colours(bool fade) noexcept {
    try {
        const HostColours colours = host_colours();
        const COLORREF panel = colours.background;
        StripTheme theme;
        theme.background = panel;
        theme.text = colours.text;
        theme.dark = colours.dark;
        theme.active_fill = static_cast<float>(settings_.accent_strength) / 100.0f;

        // The strip's own background: exactly the host's, a custom colour, or the panel with some
        // accent mixed in. Light or dark follows what is drawn.
        std::uint32_t bg = colour::rgb_from_colorref(panel);
        if (settings_.strip_background == StripBackground::custom) {
            bg = settings_.background_argb & 0xFFFFFFu;
            theme.dark = colour::lightness(bg) < colour::light_background_lightness;
        }
        const auto mix = [](std::uint32_t a, std::uint32_t b, float t) {
            const auto ch = [&](int shift) {
                const float x = static_cast<float>((a >> shift) & 0xFFu);
                const float y = static_cast<float>((b >> shift) & 0xFFu);
                return static_cast<std::uint32_t>(std::lround(x * t + y * (1.0f - t))) << shift;
            };
            return ch(16) | ch(8) | ch(0);
        };

        // Every accent clears an APCA contrast floor against the strip (fb2k-common's
        // colour.h, the same code as Media Bar and foo_onscreendisplay). A cover colour is raw,
        // so it gets the full treatment (lightness and hue search, chroma floor); the host's
        // selection colour and a custom colour are the user's choice and are only nudged when
        // they would vanish.
        std::uint32_t accent = colour::rgb_from_colorref(colours.selection);
        if (settings_.accent_source == AccentSource::custom) accent = settings_.accent_argb & 0xFFFFFFu;
        if (settings_.accent_source == AccentSource::highlight) accent = colour::rgb_from_colorref(colours.highlight);
        std::optional<std::uint32_t> cover_raw;
        float tint_scale = 1.0f;
        if (settings_.accent_source == AccentSource::cover) {
            if (const auto cover_colours = cover::current_colours(); cover_colours) {
                cover_raw = cover_colours->primary;
                accent = colour::accent_for_background(*cover_raw, bg);
                // A nearly grey cover tints the background muddy grey-brown: fade the tint out.
                tint_scale = fbc::tint_weight(cover_colours->colourfulness);
            }
        }
        accent = colour::with_min_lc(accent, bg, colour::accent_min_lc_for(bg));

        if (settings_.strip_background == StripBackground::accent_tint) {
            // Tint the background, then make sure the accent still stands out from its own tint.
            bg = mix(accent, bg, tint_scale * static_cast<float>(settings_.tint_strength) / 100.0f);
            accent = colour::with_min_lc(accent, bg, colour::accent_min_lc_for(bg));
        }
        if (settings_.strip_background != StripBackground::theme) {
            theme.text = colour::colorref_from_rgb(
                colour::with_min_lc(colour::rgb_from_colorref(theme.text), bg, colour::text_min_lc));
        }
        theme.background = colour::colorref_from_rgb(bg);
        theme.accent = colour::colorref_from_rgb(accent);
        if (cover_raw && colour::lightness(bg) >= colour::light_background_lightness) {
            // A solid fill on a light strip: the cover's colour in the light window (yellow stays
            // yellow instead of going olive); the strip picks dark text for it.
            theme.fill_accent = colour::colorref_from_rgb(colour::fill_for_cover(*cover_raw, true));
        }

        background_ = panel;
        child_background_ = colours.layout.value_or(panel);
        hot_zone_.set_colour(panel);
        strip_.set_theme(theme, fade);
        if (const HWND self = core_wnd(); self != nullptr) InvalidateRect(self, nullptr, FALSE);
    } catch (...) {
    }
}

namespace {

//! The Fonts page over the host's font: a picked family (and its size, weight and italic), then
//! the fallback families ahead of the host's own fallback (fb2k-common builds the chain).
void apply_user_font(StripFont& font, const TabFont& user) {
    if (!user.family.empty()) {
        float size = font.size_dip;
        if (size <= 0.0f && font.font.lfHeight != 0) {
            size = std::fabs(static_cast<float>(font.font.lfHeight)) * 96.0f /
                   static_cast<float>(font.font_dpi != 0 ? font.font_dpi : 96);
        }
        if (user.tenths_pt != 0) size = static_cast<float>(user.tenths_pt) / 10.0f * 96.0f / 72.0f;
        font.family = fbc::fonts::widen(user.family);
        font.weight = static_cast<DWRITE_FONT_WEIGHT>(user.weight != 0 ? user.weight : 400);
        font.style = user.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
        font.stretch = DWRITE_FONT_STRETCH_NORMAL;
        font.size_dip = size > 0.0f ? size : 12.0f;
    }
    std::vector<std::wstring> families;
    for (const std::string& name : user.fallbacks) {
        if (!name.empty()) families.push_back(fbc::fonts::widen(name));
    }
    if (families.empty()) return;
    com_ptr<IDWriteFontFallback> host;
    if (font.fallback) {
        (void)font.fallback->QueryInterface(__uuidof(IDWriteFontFallback), reinterpret_cast<void**>(host.put()));
    }
    if (auto chain = fbc::fonts::make_fallback(gfx::dwrite(), families, host.get()); chain) {
        *font.fallback.put() = chain.Detach();
    }
}

} // namespace

void TabsCore::refresh_font() noexcept {
    try {
        StripFont font;
        StripTextOptions options;
        host_font(font, options);
        applied_font_ = settings_.font;
        apply_user_font(font, settings_.font);

        const int before = strip_.thickness();
        strip_.set_text_options(options);
        strip_.set_font(font);
        if (strip_.thickness() != before && core_wnd() != nullptr) {
            layout();
            limits_changed();
        }
    } catch (...) {
    }
}

//! A new cover: the strip fades to its colours instead of switching at once.
void TabsCore::on_cover_accent_changed() noexcept { refresh_colours(true); }

void TabsCore::update_cover_subscription() noexcept {
    const bool want = core_wnd() != nullptr && settings_.accent_source == AccentSource::cover;
    if (want == cover_subscribed_) return;
    cover_subscribed_ = want;
    if (want) {
        cover::subscribe(this);
    } else {
        cover::unsubscribe(this);
    }
}

void TabsCore::apply_settings() noexcept {
    clamp(settings_);
    strip_.set_settings(strip_settings());
    if (settings_.font != applied_font_) refresh_font();
    update_cover_subscription();
    refresh_colours();
    ah_update_mode();
    layout();
    limits_changed();
    ah_evaluate();
}


// ---------------------------------------------------------------------------------------------
// Editing the tabs (the hosts' insert, remove and replace).

Tab* TabsCore::insert_tab(std::size_t index, std::unique_ptr<Tab> tab, bool activate_it) noexcept {
    if (!tab) return nullptr;
    Tab* raw = tab.get();
    try {
        index = (std::min)(index, tabs_.size());
        tabs_.insert(tabs_.begin() + static_cast<std::ptrdiff_t>(index), std::move(tab));
    } catch (...) {
        return nullptr;
    }
    if (saved_active_ >= index && saved_active_ + 1 < tabs_.size()) ++saved_active_;
    if (core_wnd() == nullptr) return raw;
    host_prepare(*raw);
    if (!settings_.lazy_children && tab_visible(*raw)) ensure_window(*raw);
    rebuild_strip();
    if (activate_it) activate(raw, true);
    ensure_active_valid();
    layout();
    limits_changed();
    return raw;
}

void TabsCore::remove_tab(std::size_t index) noexcept {
    if (index >= tabs_.size()) return;
    Tab* tab = tabs_[index].get();
    if (core_wnd() != nullptr) {
        // Move to the neighbour first, so the removal never exposes an empty area.
        if (tab == active_) {
            Tab* next = nullptr;
            for (std::size_t i = index + 1; i < tabs_.size() && next == nullptr; ++i) {
                if (tab_visible(*tabs_[i])) next = tabs_[i].get();
            }
            for (std::size_t i = index; i > 0 && next == nullptr; --i) {
                if (tab_visible(*tabs_[i - 1])) next = tabs_[i - 1].get();
            }
            activate(next, false);
        }
        destroy_tab_window(*tab);
    }
    if (active_ == tab) active_ = nullptr;
    std::erase(config_tabs_, tab);
    tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(index));
    if (saved_active_ > index) --saved_active_;
    if (core_wnd() == nullptr) return;
    rebuild_strip();
    ensure_active_valid();
    layout();
    limits_changed();
}

void TabsCore::replace_tab(std::size_t index, std::unique_ptr<Tab> tab) noexcept {
    if (!tab || index >= tabs_.size()) return;
    Tab* raw = tab.get();
    Tab* old = tabs_[index].get();
    const bool was_active = old == active_;
    if (core_wnd() != nullptr) destroy_tab_window(*old);
    if (was_active) active_ = nullptr;
    std::replace(config_tabs_.begin(), config_tabs_.end(), old, raw);
    tabs_[index] = std::move(tab);
    if (core_wnd() == nullptr) return;
    host_prepare(*raw);
    if (!settings_.lazy_children && tab_visible(*raw)) ensure_window(*raw);
    rebuild_strip();
    if (was_active) activate(raw, false);
    ensure_active_valid();
    layout();
    limits_changed();
}

void TabsCore::reorder_tabs(const std::size_t* order, std::size_t count) noexcept {
    // new[i] = old[order[i]] - what Columns UI's own splitters do (the header's wording reads the
    // other way round). Anything that is not a permutation is ignored.
    if (order == nullptr || count != tabs_.size()) return;
    try {
        std::vector<bool> seen(count, false);
        for (std::size_t i = 0; i < count; ++i) {
            if (order[i] >= count || seen[order[i]]) return;
            seen[order[i]] = true;
        }
        std::vector<std::unique_ptr<Tab>> reordered(count);
        for (std::size_t i = 0; i < count; ++i) reordered[i] = std::move(tabs_[order[i]]);
        tabs_ = std::move(reordered);
    } catch (...) {
        return;
    }
    if (core_wnd() != nullptr) rebuild_strip();
}


// ---------------------------------------------------------------------------------------------
// Strip intents.

void TabsCore::on_strip_activate(std::size_t index) noexcept {
    if (index < visible_.size()) activate(tabs_[visible_[index]].get(), true);
}

void TabsCore::on_strip_step(int direction) noexcept {
    if (visible_.empty()) return;
    const std::size_t current = strip_index_of(active_);
    std::size_t next = 0;
    if (current == no_index) {
        next = direction > 0 ? 0 : visible_.size() - 1;
    } else if (direction < 0) {
        if (current == 0) return;
        next = current - 1;
    } else {
        if (current + 1 >= visible_.size()) return;
        next = current + 1;
    }
    activate(tabs_[visible_[next]].get(), true);
}

void TabsCore::on_strip_middle_click(std::size_t index) noexcept {
    if (settings_.middle_click == MiddleClick::nothing || configuring() || index >= visible_.size()) return;
    set_tab_hidden(tabs_[visible_[index]].get(), true);
}

void TabsCore::on_strip_reorder(std::size_t from, std::size_t to) noexcept {
    if (from < visible_.size() && to < visible_.size() && from != to) {
        // Next to the tab it was dropped on; hidden tabs in between keep their place.
        move_tab(visible_[from], visible_[to]);
    } else {
        rebuild_strip();
    }
}

void TabsCore::on_strip_reorder_block(std::span<const std::size_t> moved, std::size_t neighbour,
                                      bool before) noexcept {
    // `moved` and `neighbour` index visible_, which still holds the order before the drag.
    try {
        if (neighbour >= visible_.size()) return rebuild_strip();
        const std::size_t target = visible_[neighbour];
        std::vector<bool> moving(tabs_.size(), false);
        for (const std::size_t si : moved) {
            if (si >= visible_.size() || visible_[si] == target || visible_[si] >= tabs_.size()) {
                return rebuild_strip();
            }
            moving[visible_[si]] = true;
        }
        // The block next to the target, in strip order; hidden tabs in between keep their place.
        std::vector<std::unique_ptr<Tab>> order;
        order.reserve(tabs_.size());
        const auto put_block = [&] {
            for (const std::size_t si : moved) order.push_back(std::move(tabs_[visible_[si]]));
        };
        for (std::size_t i = 0; i < tabs_.size(); ++i) {
            if (moving[i]) continue;
            if (i == target && before) put_block();
            order.push_back(std::move(tabs_[i]));
            if (i == target && !before) put_block();
        }
        tabs_.swap(order);
    } catch (const std::exception& e) {
        log::warn(std::string("moving tabs failed: ") + e.what());
    }
    rebuild_strip();
}

void TabsCore::show_tab_menu(std::size_t strip_index, POINT screen, bool with_panel_items,
                                  bool with_style) noexcept {
    const HWND self = core_wnd();
    if (self == nullptr) return;
    const auto keep_alive = host_keep_alive();
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return;
    try {
        const std::vector<std::size_t> snapshot = visible_;
        const Tab* clicked = strip_index < snapshot.size() ? tabs_[snapshot[strip_index]].get() : nullptr;
        for (std::size_t s = 0; s < snapshot.size(); ++s) {
            const Tab* tab = tabs_[snapshot[s]].get();
            const std::wstring text = menu_text(tab->label.empty() ? std::wstring(L"(untitled)") : tab->label);
            AppendMenuW(menu, MF_STRING | (tab == active_ ? MF_CHECKED : 0),
                        menu_tab_base + static_cast<UINT>(s), text.c_str());
        }
        bool child_menu = false;
        if (with_panel_items && clicked != nullptr && clicked->wnd != nullptr) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            child_menu = host_child_menu(*const_cast<Tab*>(clicked), menu, menu_panel_base, 0x7FFF0000u);
            if (!child_menu) DeleteMenu(menu, static_cast<UINT>(GetMenuItemCount(menu) - 1), MF_BYPOSITION);
        }
        std::vector<Tab*> hidden;
        if (with_style) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            // Right-click on a tab of a multiple selection: commands for the selection.
            std::vector<Tab*> selected;
            if (clicked != nullptr && strip_.is_selected(strip_index) && strip_.selection_count() >= 2) {
                std::vector<std::size_t> picked;
                strip_.selection(picked);
                for (const std::size_t si : picked) {
                    if (si < snapshot.size()) selected.push_back(tabs_[snapshot[si]].get());
                }
            }
            if (selected.size() >= 2) {
                // The last visible tab always stays.
                const UINT all = selected.size() >= snapshot.size() || configuring() ? MF_GRAYED : 0;
                const std::wstring hide = L"Hide " + std::to_wstring(selected.size()) + L" tabs";
                AppendMenuW(menu, MF_STRING | all, cmd_hide_selected, hide.c_str());
                AppendMenuW(menu, MF_STRING, cmd_clear_selection, L"Clear selection");
                selected_for_menu_ = std::move(selected);
            } else if (clicked != nullptr) {
                const bool side =
                    settings_.position == StripPosition::left || settings_.position == StripPosition::right;
                // Greyed while the Configure dialog is open: its OK or Cancel would undo these.
                const UINT editing = configuring() ? MF_GRAYED : 0;
                const UINT first = strip_index == 0 ? MF_GRAYED : editing;
                const UINT last = strip_index + 1 >= snapshot.size() ? MF_GRAYED : editing;
                AppendMenuW(menu, MF_STRING | editing, cmd_rename, L"Rename...");
                AppendMenuW(menu, MF_STRING | (snapshot.size() < 2 ? MF_GRAYED : editing), cmd_hide, L"Hide tab");
                AppendMenuW(menu, MF_STRING | first, cmd_move_back, side ? L"Move up" : L"Move left");
                AppendMenuW(menu, MF_STRING | last, cmd_move_forward, side ? L"Move down" : L"Move right");
            }
            for (const auto& tab : tabs_) {
                if (tab->extra.hidden) hidden.push_back(tab.get());
            }
            if (!hidden.empty()) {
                if (HMENU sub = CreatePopupMenu(); sub != nullptr) {
                    for (std::size_t i = 0; i < hidden.size(); ++i) {
                        std::wstring name = hidden[i]->label.empty() ? host_panel_name(*hidden[i]) : hidden[i]->label;
                        AppendMenuW(sub, MF_STRING, menu_unhide_base + static_cast<UINT>(i), menu_text(name).c_str());
                    }
                    AppendMenuW(menu, MF_POPUP | (configuring() ? MF_GRAYED : 0), reinterpret_cast<UINT_PTR>(sub),
                                L"Show hidden tab");
                }
            }
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            append_style_menu(menu);
            AppendMenuW(menu, MF_STRING, cmd_configure, L"Configure...");
        }
        const UINT cmd = static_cast<UINT>(TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD,
                                                          screen.x, screen.y, 0, self, nullptr));
        DestroyMenu(menu);
        menu = nullptr;
        if (cmd >= menu_panel_base) {
            if (child_menu) host_child_menu_command(cmd);
        } else if (cmd >= menu_style_base && cmd < style_last) {
            run_style_command(cmd);
        } else if (cmd >= menu_unhide_base && cmd < menu_style_base) {
            const std::size_t h = cmd - menu_unhide_base;
            if (h < hidden.size()) set_tab_hidden(hidden[h], false);
        } else if (cmd >= menu_cmd_base && cmd < menu_unhide_base) {
            Tab* tab = const_cast<Tab*>(clicked);
            const bool valid = tab != nullptr && snapshot == visible_;
            switch (cmd) {
            case cmd_rename:
                if (valid) rename_tab(tab);
                break;
            case cmd_hide:
                if (valid) set_tab_hidden(tab, true);
                break;
            case cmd_move_back:
                if (valid && strip_index > 0) move_tab(snapshot[strip_index], snapshot[strip_index - 1]);
                break;
            case cmd_move_forward:
                if (valid && strip_index + 1 < snapshot.size()) {
                    move_tab(snapshot[strip_index], snapshot[strip_index + 1]);
                }
                break;
            case cmd_configure: run_configure(self, true); break;
            case cmd_hide_selected:
                // By tab, not index: each hide rebuilds the strip. set_tab_hidden keeps the last one.
                for (Tab* t : selected_for_menu_) {
                    if (index_of(t) != no_index) set_tab_hidden(t, true);
                }
                strip_.clear_selection();
                break;
            case cmd_clear_selection: strip_.clear_selection(); break;
            default: break;
            }
        } else if (cmd >= menu_tab_base && cmd < menu_cmd_base) {
            const std::size_t s = cmd - menu_tab_base;
            // The tab list may have changed while the menu was open.
            if (s < snapshot.size() && snapshot == visible_) activate(tabs_[snapshot[s]].get(), true);
        }
    } catch (const std::exception& e) {
        log::warn(std::string("tab menu failed: ") + e.what());
    }
    if (menu != nullptr) DestroyMenu(menu);
    selected_for_menu_.clear();
    host_child_menu_done();
}

void TabsCore::on_strip_menu(std::size_t index, POINT screen) noexcept {
    if (host_strip_menu(index, screen)) return;
    // Pins an auto-hidden strip for the menu and any dialog it opens.
    menu_pin_ = true;
    show_tab_menu(index != no_index ? index : strip_index_of(active_), screen, index != no_index, true);
    menu_pin_ = false;
    ah_evaluate();
}

void TabsCore::on_strip_overflow(POINT screen) noexcept {
    menu_pin_ = true;
    show_tab_menu(no_index, screen, false, false);
    menu_pin_ = false;
    ah_evaluate();
}

void TabsCore::append_style_menu(HMENU menu) const noexcept {
    HMENU style = CreatePopupMenu();
    if (style == nullptr) return;
    const auto radio = [](HMENU m, unsigned id, const wchar_t* text, bool on) {
        AppendMenuW(m, MF_STRING | (on ? MF_CHECKED : 0), id, text);
        if (on) {
            MENUITEMINFOW mii{sizeof(mii)};
            mii.fMask = MIIM_FTYPE;
            mii.fType = MFT_STRING | MFT_RADIOCHECK;
            SetMenuItemInfoW(m, id, FALSE, &mii);
        }
    };
    const auto sub = [&](const wchar_t* text) {
        HMENU m = CreatePopupMenu();
        if (m != nullptr) AppendMenuW(style, MF_POPUP, reinterpret_cast<UINT_PTR>(m), text);
        return m;
    };
    const Settings& s = settings_;
    if (HMENU m = sub(L"Strip position"); m != nullptr) {
        radio(m, style_position_top, L"Top", s.position == StripPosition::top);
        radio(m, style_position_bottom, L"Bottom", s.position == StripPosition::bottom);
        radio(m, style_position_left, L"Left", s.position == StripPosition::left);
        radio(m, style_position_right, L"Right", s.position == StripPosition::right);
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        const bool side = s.position == StripPosition::left || s.position == StripPosition::right;
        AppendMenuW(m, MF_STRING | (s.side_text == SideText::rotated ? MF_CHECKED : 0) | (side ? 0 : MF_GRAYED),
                    style_rotate_side_text, L"Rotate text on side strips");
    }
    if (HMENU m = sub(L"Active tab"); m != nullptr) {
        radio(m, style_indicator_underline, L"Underline", s.indicator == Indicator::underline);
        radio(m, style_indicator_pill, L"Pill", s.indicator == Indicator::pill);
        radio(m, style_indicator_tab, L"Tab", s.indicator == Indicator::tab);
        radio(m, style_indicator_tab_outline, L"Outlined tab", s.indicator == Indicator::tab_outline);
        radio(m, style_indicator_none, L"Text only", s.indicator == Indicator::none);
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING | (s.chip ? MF_CHECKED : 0), style_chip, L"Chips");
    }
    if (HMENU m = sub(L"Accent colour"); m != nullptr) {
        const std::wstring text = std::wstring(host_ui_name()) + L" selection colour";
        radio(m, style_accent_selection, text.c_str(), s.accent_source == AccentSource::selection);
        const std::wstring highlight = std::wstring(host_ui_name()) + L" " + host_highlight_name();
        radio(m, style_accent_highlight, highlight.c_str(), s.accent_source == AccentSource::highlight);
        radio(m, style_accent_cover, L"From the playing cover", s.accent_source == AccentSource::cover);
        radio(m, style_accent_custom, L"Custom...", s.accent_source == AccentSource::custom);
    }
    if (HMENU m = sub(L"Accent strength"); m != nullptr) {
        // Opacity of the active tab's fill; the underline is always solid.
        const bool filled = s.indicator == Indicator::pill || s.indicator == Indicator::tab;
        const UINT grey = filled ? 0 : MF_GRAYED;
        const auto level = [&](unsigned id, const wchar_t* text, bool on) {
            radio(m, id, text, on);
            if (grey != 0) EnableMenuItem(m, id, MF_BYCOMMAND | MF_GRAYED);
        };
        const std::uint8_t a = s.accent_strength;
        level(style_strength_auto, L"Automatic", a == 0);
        level(style_strength_subtle, L"Subtle (15%)", a == 15);
        level(style_strength_medium, L"Medium (35%)", a == 35);
        level(style_strength_strong, L"Strong (60%)", a == 60);
        level(style_strength_solid, L"Solid", a == 100);
    }
    if (HMENU m = sub(L"Strip background"); m != nullptr) {
        const std::wstring text = std::wstring(host_ui_name()) + L" background";
        radio(m, style_background_theme, text.c_str(), s.strip_background == StripBackground::theme);
        radio(m, style_background_tint, L"Tinted with the accent", s.strip_background == StripBackground::accent_tint);
        radio(m, style_background_custom, L"Custom...", s.strip_background == StripBackground::custom);
    }
    if (HMENU m = sub(L"Tab width"); m != nullptr) {
        radio(m, style_sizing_fit, L"Fit the title", s.sizing == TabSizing::fit);
        radio(m, style_sizing_equal, L"All equal", s.sizing == TabSizing::equal);
        radio(m, style_sizing_fill, L"Fill the strip", s.sizing == TabSizing::fill);
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        const bool slack = s.sizing != TabSizing::fill;
        const UINT grey = slack ? 0 : MF_GRAYED;
        AppendMenuW(m, MF_STRING | grey | (s.align == TabAlign::start ? MF_CHECKED : 0), style_align_start,
                    L"Align to start");
        AppendMenuW(m, MF_STRING | grey | (s.align == TabAlign::centre ? MF_CHECKED : 0), style_align_centre,
                    L"Centre");
        AppendMenuW(m, MF_STRING | grey | (s.align == TabAlign::end ? MF_CHECKED : 0), style_align_end,
                    L"Align to end");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING | (s.chevron_position == ChevronPosition::start ? MF_CHECKED : 0),
                    style_chevron_start, L"Overflow chevron at the start");
        AppendMenuW(m, MF_STRING | (s.shrink_titles ? MF_CHECKED : 0), style_shrink_titles,
                    L"Shorten titles before showing the chevron");
    }
    if (HMENU m = sub(L"Show strip"); m != nullptr) {
        radio(m, style_show_always, L"Always", s.visibility == StripVisibility::always);
        radio(m, style_show_two_or_more, L"Only with two or more tabs",
              s.visibility == StripVisibility::two_or_more);
        radio(m, style_show_auto_hide, L"Auto-hide", s.visibility == StripVisibility::auto_hide);
    }
    // Greyed while the Configure dialog is open: its OK or Cancel would undo these.
    AppendMenuW(menu, MF_POPUP | (configuring() ? MF_GRAYED : 0), reinterpret_cast<UINT_PTR>(style), L"Appearance");
}

void TabsCore::run_style_command(unsigned command) noexcept {
    Settings& s = settings_;
    switch (command) {
    case style_position_top: s.position = StripPosition::top; break;
    case style_position_bottom: s.position = StripPosition::bottom; break;
    case style_position_left: s.position = StripPosition::left; break;
    case style_position_right: s.position = StripPosition::right; break;
    case style_rotate_side_text:
        s.side_text = s.side_text == SideText::rotated ? SideText::horizontal : SideText::rotated;
        break;
    case style_indicator_underline: s.indicator = Indicator::underline; break;
    case style_indicator_pill: s.indicator = Indicator::pill; break;
    case style_indicator_none: s.indicator = Indicator::none; break;
    case style_indicator_tab: s.indicator = Indicator::tab; break;
    case style_indicator_tab_outline: s.indicator = Indicator::tab_outline; break;
    case style_chip: s.chip = !s.chip; break;
    case style_accent_selection: s.accent_source = AccentSource::selection; break;
    case style_accent_highlight: s.accent_source = AccentSource::highlight; break;
    case style_accent_cover: s.accent_source = AccentSource::cover; break;
    case style_accent_custom: {
        if (!pick_colour(s.accent_argb)) return;
        s.accent_source = AccentSource::custom;
        break;
    }
    case style_strength_auto: s.accent_strength = 0; break;
    case style_strength_subtle: s.accent_strength = 15; break;
    case style_strength_medium: s.accent_strength = 35; break;
    case style_strength_strong: s.accent_strength = 60; break;
    case style_strength_solid: s.accent_strength = 100; break;
    case style_background_theme: s.strip_background = StripBackground::theme; break;
    case style_background_tint: s.strip_background = StripBackground::accent_tint; break;
    case style_background_custom: {
        if (!pick_colour(s.background_argb)) return;
        s.strip_background = StripBackground::custom;
        break;
    }
    case style_sizing_fit: s.sizing = TabSizing::fit; break;
    case style_sizing_equal: s.sizing = TabSizing::equal; break;
    case style_sizing_fill: s.sizing = TabSizing::fill; break;
    case style_align_start: s.align = TabAlign::start; break;
    case style_align_centre: s.align = TabAlign::centre; break;
    case style_align_end: s.align = TabAlign::end; break;
    case style_chevron_start:
        s.chevron_position =
            s.chevron_position == ChevronPosition::start ? ChevronPosition::end : ChevronPosition::start;
        break;
    case style_shrink_titles: s.shrink_titles = !s.shrink_titles; break;
    case style_show_always: s.visibility = StripVisibility::always; break;
    case style_show_two_or_more: s.visibility = StripVisibility::two_or_more; break;
    case style_show_auto_hide: s.visibility = StripVisibility::auto_hide; break;
    default: return;
    }
    apply_settings();
}

bool TabsCore::run_configure(HWND parent, bool modeless) {
    const auto keep_alive = host_keep_alive();
    if (configure_wnd_ != nullptr) {
        // One dialog per container: bring back the open one.
        if (IsIconic(configure_wnd_) != FALSE) ShowWindow(configure_wnd_, SW_RESTORE);
        SetForegroundWindow(configure_wnd_);
        return false;
    }
    ConfigureState original;
    original.settings = settings_;
    original.ui_name = host_ui_name();
    original.highlight_name = host_highlight_name();
    try {
        // As refresh_colours decides it: a custom background has its own lightness.
        original.dark = settings_.strip_background == StripBackground::custom
                            ? colour::lightness(settings_.background_argb & 0xFFFFFFu) <
                                  colour::light_background_lightness
                            : host_colours().dark;
    } catch (...) {
    }
    try {
        StripFont font;
        StripTextOptions options;
        host_font(font, options);
        original.host_font_family = !font.family.empty() ? font.family : std::wstring(font.font.lfFaceName);
        // A GDI font keeps whole pixels: read back the size it was most likely picked at (8 pt,
        // not the 8.3 that 11 px at 96 DPI works out to).
        original.host_font_tenths =
            font.size_dip > 0.0f ? static_cast<std::uint32_t>(std::lround(font.size_dip * 72.0f / 96.0f * 10.0f))
                                 : tenths_from_pixels(std::fabs(static_cast<float>(font.font.lfHeight)), font.font_dpi);
    } catch (...) {
    }
    try {
        config_tabs_.clear();
        for (std::size_t i = 0; i < tabs_.size(); ++i) {
            config_tabs_.push_back(tabs_[i].get());
            original.tabs.push_back(TabEdit{i, host_panel_name(*tabs_[i]), tabs_[i]->extra});
        }
    } catch (...) {
        config_tabs_.clear();
        return false;
    }
    const bool live = core_wnd() != nullptr;
    // Owned by the window the user is in: Columns UI's Layout page passes the main window,
    // which would put the dialog behind Preferences.
    HWND owner = GetActiveWindow();
    if (owner == nullptr || IsWindowEnabled(owner) == FALSE) {
        owner = parent != nullptr ? GetAncestor(parent, GA_ROOT) : core_api::get_main_window();
    }
    if (modeless && live) {
        try {
            configure_original_ = original;
            configure_wnd_ = open_configure_dialog(owner, original, *this);
        } catch (const std::exception& e) {
            log::warn(std::string("the Configure dialog failed: ") + e.what());
        }
        if (configure_wnd_ != nullptr) {
            strip_.set_settings(strip_settings()); // no drag reordering meanwhile
            ah_evaluate();                         // an auto-hidden strip stays shown while the dialog is open
            return false;
        }
    }
    ConfigureState state = original;
    bool ok = false;
    const bool pinned = menu_pin_; // already set when opened from the tab menu
    menu_pin_ = true;              // an auto-hidden strip stays shown while the dialog is open
    try {
        ok = run_configure_dialog(owner, state, *this, live);
    } catch (const std::exception& e) {
        log::warn(std::string("the Configure dialog failed: ") + e.what());
    }
    menu_pin_ = pinned;
    if (!pinned) ah_evaluate();
    // Cancel puts back what the live preview changed.
    preview(ok ? state : original);
    if (ok) commit_removals();
    config_tabs_.clear();
    return ok;
}

void TabsCore::configure_closed(bool ok, const ConfigureState& state) noexcept {
    configure_wnd_ = nullptr;
    // Cancel puts back what the live preview changed.
    preview(ok ? state : configure_original_);
    if (ok) commit_removals();
    config_tabs_.clear();
    ah_evaluate();
}

Settings TabsCore::strip_settings() const noexcept {
    Settings s = settings_;
    if (configuring()) s.drag_reorder = false;
    return s;
}

void TabsCore::commit_removals() noexcept {
    bool removed = false;
    for (std::size_t i = tabs_.size(); i-- > 0;) {
        Tab* tab = tabs_[i].get();
        if (!tab->pending_removal) continue;
        if (tab == active_) active_ = nullptr;
        if (core_wnd() != nullptr) destroy_tab_window(*tab);
        tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(i));
        if (saved_active_ > i) --saved_active_;
        removed = true;
    }
    if (!removed || core_wnd() == nullptr) return;
    rebuild_strip();
    ensure_active_valid();
    layout();
    limits_changed();
}

void TabsCore::preview(const ConfigureState& state) noexcept {
    try {
        settings_ = state.settings;
        clamp(settings_);
        // The dialog's order, if it still describes the tabs this container has. Tabs the
        // dialog removed go to the end, marked; they are deleted only on OK.
        const std::size_t count = tabs_.size();
        bool same = state.tabs.size() <= count && config_tabs_.size() == count;
        std::vector<size_t> order;
        std::vector<bool> kept(count, false);
        for (std::size_t i = 0; same && i < state.tabs.size(); ++i) {
            const std::size_t id = state.tabs[i].id;
            const std::size_t index = id < config_tabs_.size() ? index_of(config_tabs_[id]) : no_index;
            if (index == no_index || kept[index]) {
                same = false;
                break;
            }
            kept[index] = true;
            order.push_back(index);
        }
        if (same) {
            for (std::size_t i = 0; i < count; ++i) {
                if (!kept[i]) order.push_back(i);
            }
            reorder_tabs(order.data(), order.size());
            for (std::size_t i = 0; i < count; ++i) {
                Tab& tab = *tabs_[i];
                tab.pending_removal = i >= state.tabs.size();
                if (tab.pending_removal) continue;
                tab.extra = state.tabs[i].extra;
                update_label(tab);
            }
        }
        if (core_wnd() == nullptr) return;
        strip_.set_settings(strip_settings());
        if (settings_.font != applied_font_) refresh_font();
        update_cover_subscription();
        refresh_colours();
        if (!settings_.lazy_children) {
            for (auto& tab : tabs_) {
                if (tab_visible(*tab)) ensure_window(*tab);
            }
        }
        ah_update_mode();
        rebuild_strip();
        ensure_active_valid();
        layout();
        limits_changed();
        ah_evaluate();
    } catch (const std::exception& e) {
        log::warn(std::string("could not apply the settings: ") + e.what());
    }
}

bool TabsCore::on_strip_key(UINT message, WPARAM key) noexcept {
    try {
        if (message == WM_KEYDOWN && key == VK_TAB) {
            host_tab_key(strip_.hwnd());
            return true;
        }
        return host_shortcut(key);
    } catch (...) {
        return false;
    }
}

void TabsCore::on_strip_metrics_changed() noexcept {
    // A DPI change usually means another monitor: its text rendering parameters differ.
    refresh_font();
    layout();
    limits_changed();
}

// ---------------------------------------------------------------------------------------------
// Auto-hide. Event driven: the hot zone and the strip report the pointer
// (TrackMouseEvent), menus, drags and focus pin it. One delay timer and, while a show/hide
// animation runs, one frame timer. Nothing runs while idle.

int TabsCore::ah_px(unsigned dip) const noexcept {
    const unsigned dpi = core_wnd() != nullptr ? gfx::window_dpi(core_wnd()) : gfx::system_dpi();
    return MulDiv(static_cast<int>(dip), static_cast<int>(dpi), 96);
}

void TabsCore::ah_update_mode() noexcept {
    const HWND self = core_wnd();
    if (self == nullptr || strip_.hwnd() == nullptr || !auto_hide()) {
        if (self != nullptr) {
            KillTimer(self, timer_ah_delay);
            KillTimer(self, timer_ah_frame);
        }
        ah_timer_ = AhTimer::none;
        ah_animating_ = false;
        ah_shown_ = false;
        ah_progress_ = 0.0f;
        hot_zone_.destroy();
        ah_sync_parent_watch();
        (void)strip_.set_layered(false);
        return;
    }
    // Child layered windows are Windows 8+. Ask for the invisible hot zone there; if Windows
    // grants it, the strip can be layered too and go over the panel.
    if (hot_zone_.hwnd() == nullptr) (void)hot_zone_.create(self, *this, gfx::layered_children_supported());
    ah_sync_parent_watch();
    hot_zone_.set_colour(background_);
    hot_zone_.set_transparent(settings_.transparent_background);
    const bool want_layered = settings_.reveal_mode == RevealMode::overlay && hot_zone_.layered();
    if (!strip_.set_layered(want_layered) && want_layered) log::warn("auto-hide: the strip could not be layered; pushing the panel instead");
    if (settings_.show_hide_animation == ShowHideAnimation::none || !ah_overlay()) {
        KillTimer(self, timer_ah_frame);
        ah_animating_ = false;
        ah_progress_ = ah_shown_ ? 1.0f : 0.0f;
    }
}

bool TabsCore::ah_covered() const noexcept {
    const HWND self = core_wnd();
    if (self == nullptr) return false;
    const HWND strip = strip_.hwnd();
    const HWND zone = hot_zone_.hwnd();
    int pending = (strip != nullptr ? 1 : 0) + (zone != nullptr ? 1 : 0);
    // Ours must be the first children in z-order. Hidden ones count: a hot zone left under a
    // panel while the strip is shown would be just as dead once it is shown again.
    for (HWND wnd = GetWindow(self, GW_CHILD); wnd != nullptr && pending > 0; wnd = GetWindow(wnd, GW_HWNDNEXT)) {
        if (wnd != strip && wnd != zone) return true;
        --pending;
    }
    return false;
}

void TabsCore::ah_raise() noexcept {
    if (!ah_covered()) return;
    // Hot zone, then strip, so the strip ends up first.
    for (const HWND wnd : {hot_zone_.hwnd(), strip_.hwnd()}) {
        if (wnd == nullptr) continue;
        SetWindowPos(wnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
}

// SetParent puts the window on top of its new siblings, and nothing tells the parent: no
// message, no EVENT_OBJECT_REORDER (test/zorder_test.cpp). Visualisations with their own
// fullscreen mode may reparent their window to a monitor-sized popup and back, which left it
// over the hot zone, so the strip could no longer be revealed. EVENT_OBJECT_PARENTCHANGE does report it. One out-of-context hook for
// the process, only while some container has a hot zone; the callback runs from our message
// loop after the reparenting call has returned.
namespace {
HWINEVENTHOOK g_parent_watch = nullptr;
#ifndef EVENT_OBJECT_PARENTCHANGE
constexpr DWORD EVENT_OBJECT_PARENTCHANGE = 0x800F;
#endif
} // namespace

void TabsCore::ah_sync_parent_watch() noexcept {
    bool wanted = false;
    for (const TabsCore* core : live()) wanted = wanted || core->hot_zone_.hwnd() != nullptr;
    if (wanted && g_parent_watch == nullptr) {
        g_parent_watch = SetWinEventHook(EVENT_OBJECT_PARENTCHANGE, EVENT_OBJECT_PARENTCHANGE, nullptr,
                                         &TabsCore::ah_on_parent_change, GetCurrentProcessId(), 0,
                                         WINEVENT_OUTOFCONTEXT);
        if (g_parent_watch == nullptr) log::warn("auto-hide: could not watch for panels being reparented");
    } else if (!wanted && g_parent_watch != nullptr) {
        UnhookWinEvent(g_parent_watch);
        g_parent_watch = nullptr;
    }
}

void CALLBACK TabsCore::ah_on_parent_change(HWINEVENTHOOK, DWORD event, HWND wnd, LONG object, LONG, DWORD,
                                            DWORD) noexcept {
    if (event != EVENT_OBJECT_PARENTCHANGE || object != OBJID_WINDOW || wnd == nullptr) return;
    const HWND parent = GetAncestor(wnd, GA_PARENT);
    if (parent == nullptr) return;
    for (TabsCore* core : live()) {
        if (core->core_wnd() == parent && core->hot_zone_.hwnd() != nullptr) core->ah_raise();
    }
}

bool TabsCore::ah_pointer_or_pinned() const noexcept {
    const HWND strip = strip_.hwnd();
    if (menu_pin_ || configure_wnd_ != nullptr || strip_.dragging()) return true;
    if (strip != nullptr && (GetCapture() == strip || GetFocus() == strip)) return true;
    POINT pt{};
    if (!GetCursorPos(&pt)) return false;
    for (const HWND wnd : {strip, hot_zone_.hwnd()}) {
        if (wnd == nullptr || !IsWindowVisible(wnd)) continue;
        RECT rc{};
        if (GetWindowRect(wnd, &rc) && PtInRect(&rc, pt)) return true;
    }
    return false;
}

void TabsCore::ah_set_timer(AhTimer kind, unsigned ms) noexcept {
    const HWND self = core_wnd();
    ah_timer_ = kind;
    if (self == nullptr) return;
    if (kind == AhTimer::none) {
        KillTimer(self, timer_ah_delay);
    } else {
        SetTimer(self, timer_ah_delay, (std::max)(ms, static_cast<unsigned>(USER_TIMER_MINIMUM)), nullptr);
    }
}

void TabsCore::ah_evaluate() noexcept {
    if (!auto_hide() || core_wnd() == nullptr || strip_.hwnd() == nullptr) return;
    if (ah_pointer_or_pinned()) {
        if (ah_shown_) {
            if (ah_timer_ == AhTimer::hide) ah_set_timer(AhTimer::none, 0);
            return;
        }
        if (settings_.reveal_delay_ms == 0) {
            ah_set_timer(AhTimer::none, 0);
            ah_set_shown(true);
        } else if (ah_timer_ != AhTimer::reveal) {
            ah_set_timer(AhTimer::reveal, settings_.reveal_delay_ms);
        }
        return;
    }
    if (!ah_shown_) {
        if (ah_timer_ == AhTimer::reveal) ah_set_timer(AhTimer::none, 0);
        return;
    }
    if (ah_timer_ == AhTimer::hide) return;
    unsigned delay = settings_.hide_delay_ms;
    const ULONGLONG now = GetTickCount64();
    if (linger_until_ > now) delay = (std::max)(delay, static_cast<unsigned>(linger_until_ - now));
    if (delay == 0) {
        ah_set_timer(AhTimer::none, 0);
        ah_set_shown(false);
    } else {
        ah_set_timer(AhTimer::hide, delay);
    }
}

void TabsCore::ah_set_shown(bool shown) noexcept {
    const HWND self = core_wnd();
    if (self == nullptr || shown == ah_shown_) return;
    ah_shown_ = shown;
    const bool animate = ah_overlay() && settings_.show_hide_animation != ShowHideAnimation::none;
    if (animate) {
        ah_from_ = ah_progress_;
        ah_anim_start_ = GetTickCount64();
        ah_animating_ = true;
        SetTimer(self, timer_ah_frame, USER_TIMER_MINIMUM, nullptr);
    } else {
        KillTimer(self, timer_ah_frame);
        ah_animating_ = false;
        ah_progress_ = shown ? 1.0f : 0.0f;
    }
    layout();
    if (shown) strip_.track_pointer();
}

void TabsCore::ah_on_timer(UINT_PTR id) noexcept {
    const HWND self = core_wnd();
    if (self == nullptr) return;
    if (id == timer_ah_delay) {
        const AhTimer kind = ah_timer_;
        ah_set_timer(AhTimer::none, 0);
        const bool want = ah_pointer_or_pinned();
        if (kind == AhTimer::reveal && want) ah_set_shown(true);
        if (kind == AhTimer::hide && !want) ah_set_shown(false);
        return;
    }
    if (id != timer_ah_frame) return;
    const float target = ah_shown_ ? 1.0f : 0.0f;
    // A reversal mid-way takes only the remaining share of the duration.
    const float span = (std::max)(0.05f, std::fabs(target - ah_from_));
    const float duration = static_cast<float>(settings_.animation_ms) * span;
    const float t = std::clamp(static_cast<float>(GetTickCount64() - ah_anim_start_) / duration, 0.0f, 1.0f);
    const float eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); // ease-out cubic
    ah_progress_ = ah_from_ + (target - ah_from_) * eased;
    if (t >= 1.0f) {
        ah_progress_ = target;
        ah_animating_ = false;
        KillTimer(self, timer_ah_frame);
    }
    layout();
    // A slide ends under the pointer: arm the leave tracking again.
    if (!ah_animating_ && ah_shown_) strip_.track_pointer();
}

void TabsCore::ah_note_switch() noexcept {
    if (!auto_hide() || core_wnd() == nullptr) return;
    linger_until_ = GetTickCount64() + settings_.linger_ms;
    if (!ah_shown_ && settings_.linger_ms != 0) {
        ah_set_timer(AhTimer::none, 0);
        ah_set_shown(true);
    }
    ah_evaluate();
}

void TabsCore::on_strip_pointer() noexcept { ah_evaluate(); }

void TabsCore::on_strip_paint_failed(const char* detail) noexcept {
    try {
        log::warn(std::string("the tab strip could not paint: ") + (detail != nullptr ? detail : "?"));
    } catch (...) {
    }
}

void TabsCore::on_hot_zone(bool, bool clicked) noexcept {
    if (clicked && auto_hide() && !ah_shown_) {
        // A click in the hot zone reveals at once, whatever the delay.
        ah_set_timer(AhTimer::none, 0);
        ah_set_shown(true);
        return;
    }
    ah_evaluate();
}


} // namespace bettertabs
