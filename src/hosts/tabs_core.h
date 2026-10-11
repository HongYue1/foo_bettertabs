#pragma once

// The part of a Better Tabs container that does not care which UI hosts it: the tabs, the strip,
// switching, layout, auto-hide, colours, menus, the Configure dialog, follow playback and
// Ctrl+Tab. The Columns UI container (container.cpp) and the Default UI element
// (dui_container.cpp) derive from it and supply the children and the host's colours and fonts
// through the host_* hooks.
//
// Performance shape (see README, Performance):
//  - a child's window is created the first time its tab is shown (lazy_children), never before;
//  - a switch is one DeferWindowPos batch: show the new child (moved only if its rectangle went
//    stale), hide the old one. Nothing else is touched and nothing is invalidated;
//  - a resize moves the strip and the active child only; hidden children catch up when shown;
//  - no timers, hooks or polling while idle.

#include <helpers/foobar2000+atl.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../model/codec.h"
#include "../model/settings.h"
#include "../platform/cover_hub.h"
#include "../strip/hot_zone.h"
#include "../strip/strip_window.h"
#include "configure_dialog.h"

namespace bettertabs {

inline constexpr LONG limit_cap = MAXSHORT;

struct Limits {
    unsigned min_width{0};
    unsigned min_height{0};
    unsigned max_width{static_cast<unsigned>(limit_cap)};
    unsigned max_height{static_cast<unsigned>(limit_cap)};
    [[nodiscard]] bool operator==(const Limits&) const = default;
};

//! One tab. Each host derives its own with the child's handle (a uie::window, a DUI instance).
struct Tab {
    Tab() = default;
    Tab(const Tab&) = delete;
    Tab& operator=(const Tab&) = delete;
    virtual ~Tab() = default;

    GUID guid{};
    //! The child's config as last read from it (or from the layout, before it existed).
    Bytes config;
    TabExtra extra;
    HWND wnd{nullptr};
    //! The rectangle last given to wnd; a hidden child keeps a stale one until it is shown.
    RECT applied{};
    Limits limits{};
    std::wstring label;
    //! The compiled title when it is title formatting, and the text it was compiled from.
    titleformat_object::ptr script;
    std::string script_source;
    //! Removed in the open Configure dialog: not shown, deleted when it closes with OK.
    bool pending_removal{false};
};

enum class PlaybackEvent : std::uint8_t { started, stopped, title };

//! The host's own colours, before Better Tabs' settings are applied.
struct HostColours {
    COLORREF background{RGB(255, 255, 255)};
    COLORREF text{RGB(0, 0, 0)};
    COLORREF selection{RGB(0, 120, 215)};
    //! Default UI: the highlight colour. Columns UI: the active item frame.
    COLORREF highlight{RGB(0, 120, 215)};
    //! What transparent children (splitters) show through, so the gaps between their panels -
    //! the dividers - look as they do elsewhere in the host's layout. Unset: `background`.
    std::optional<COLORREF> layout;
    bool dark{false};
};

class TabsCore : protected StripListener,
                 protected HotZoneListener,
                 protected cover::Listener,
                 protected ConfigureTarget {
public:
    TabsCore();
    virtual ~TabsCore();
    TabsCore(const TabsCore&) = delete;
    TabsCore& operator=(const TabsCore&) = delete;

    //! Every container that exists, Columns UI and Default UI alike. Main thread.
    [[nodiscard]] static const std::vector<TabsCore*>& live() noexcept;

    [[nodiscard]] virtual HWND core_wnd() const noexcept = 0;
    //! From the process-wide playback watch: follow playback, re-evaluate formatted titles.
    void on_playback(PlaybackEvent event) noexcept;
    //! Ctrl+Tab: the next (+1) or previous (-1) tab, wrapping. False when this container does
    //! not take it (switched off, fewer than two tabs), so an outer one can.
    bool cycle_tabs(int direction) noexcept;
    //! `fade`: animate to the new colours (Settings::animations permitting).
    void refresh_colours(bool fade = false) noexcept;
    void refresh_font() noexcept;

protected:
    // Host hooks ---------------------------------------------------------------------------

    [[nodiscard]] virtual std::unique_ptr<Tab> host_new_tab() const = 0;
    //! Gets the child ready to have a tab (Columns UI: its extension object). False: it has none.
    virtual bool host_prepare(Tab& tab) noexcept = 0;
    //! The child can have a tab (prepared and available).
    [[nodiscard]] virtual bool host_present(const Tab& tab) const noexcept = 0;
    //! Creates the child's window in content_ and returns it (the core hides it if need be).
    virtual HWND host_create_window(Tab& tab) noexcept = 0;
    //! Destroys the child's window (and object), keeping its configuration in tab.config.
    virtual void host_destroy(Tab& tab) noexcept = 0;
    //! The child's current configuration: read from it while it lives, else tab.config.
    [[nodiscard]] virtual Bytes host_child_config(const Tab& tab) const = 0;
    //! The tab's default title (no custom title): the child's short name.
    [[nodiscard]] virtual std::wstring host_child_label(const Tab& tab) const = 0;
    //! The child's full name for dialogs and menus.
    [[nodiscard]] virtual std::wstring host_panel_name(const Tab& tab) const = 0;
    //! Reads tab.limits from the child; the default asks its window (WM_GETMINMAXINFO).
    virtual void host_query_limits(Tab& tab) noexcept;
    //! Our size limits changed: tell the parent.
    virtual void host_limits_changed() noexcept = 0;
    //! The container is shown (by the host's rules, not only IsWindowVisible).
    [[nodiscard]] virtual bool host_visible() const noexcept = 0;
    [[nodiscard]] virtual HostColours host_colours() const noexcept = 0;
    virtual void host_font(StripFont& font, StripTextOptions& options) const noexcept = 0;
    //! "Columns UI" or "Default UI", for menu and dialog wording.
    [[nodiscard]] virtual const wchar_t* host_ui_name() const noexcept = 0;
    //! The UI's own name for HostColours::highlight, after host_ui_name().
    [[nodiscard]] virtual const wchar_t* host_highlight_name() const noexcept = 0;
    //! A created child became the shown tab (true) or stopped being it (false).
    virtual void host_child_shown(Tab&, bool) noexcept {}
    //! The child's own menu items for the strip menu, ids in [first, last). True if any.
    virtual bool host_child_menu(Tab&, HMENU, unsigned, unsigned) noexcept { return false; }
    virtual void host_child_menu_command(unsigned) noexcept {}
    virtual void host_child_menu_done() noexcept {}
    //! Right click on the strip: true if the host takes it (Default UI's layout editing).
    virtual bool host_strip_menu(std::size_t, POINT) noexcept { return false; }
    //! Tab on the focused strip: move the focus on.
    virtual void host_tab_key(HWND from) noexcept = 0;
    //! A key the strip does not use: run foobar2000's keyboard shortcuts. True if one ran.
    virtual bool host_shortcut(WPARAM key) noexcept = 0;
    //! Holds a reference on the container while a menu or dialog runs.
    [[nodiscard]] virtual service_ptr_t<service_base> host_keep_alive() noexcept = 0;
    //! Settings or tabs changed in a way the host may want to store (Default UI: nothing to do).
    virtual void host_on_create() noexcept {}
    virtual void host_on_destroy() noexcept {}

    // Configuration --------------------------------------------------------------------------

    void load(InstanceData&& data);
    [[nodiscard]] InstanceData snapshot(bool refresh_children) const;
    //! Replaces everything with `data`, live if the window exists.
    void reload(InstanceData&& data);

    // Children -------------------------------------------------------------------------------

    bool ensure_window(Tab& tab) noexcept;
    void destroy_tab_window(Tab& tab) noexcept;
    void update_label(Tab& tab) noexcept;
    //! Re-evaluates the title-formatted labels; rebuilds the strip if one changed.
    void refresh_titles() noexcept;
    [[nodiscard]] bool tab_visible(const Tab& tab) const noexcept;
    [[nodiscard]] Tab* find_by_wnd(HWND wnd) const noexcept;
    [[nodiscard]] std::size_t index_of(const Tab* tab) const noexcept;
    [[nodiscard]] std::size_t strip_index_of(const Tab* tab) const noexcept;
    void set_tab_hidden(Tab* tab, bool hidden) noexcept;
    //! Moves tabs_[from] to position `to`.
    void move_tab(std::size_t from, std::size_t to) noexcept;
    //! new[i] = old[order[i]]; anything that is not a permutation is ignored.
    void reorder_tabs(const std::size_t* order, std::size_t count) noexcept;
    void rename_tab(Tab* tab) noexcept;
    //! Inserts a tab at `index` (clamped) and shows it if the window exists.
    Tab* insert_tab(std::size_t index, std::unique_ptr<Tab> tab, bool activate_it) noexcept;
    //! Removes tabs_[index], moving to a neighbour first if it was shown.
    void remove_tab(std::size_t index) noexcept;
    //! Replaces tabs_[index] with `tab`, keeping it shown if it was.
    void replace_tab(std::size_t index, std::unique_ptr<Tab> tab) noexcept;
    //! Deletes the tabs the Configure dialog removed.
    void commit_removals() noexcept;
    void on_child_limits_changed(HWND wnd) noexcept;
    bool show_child(HWND wnd) noexcept;

    // Switching and layout -------------------------------------------------------------------

    void rebuild_strip() noexcept;
    [[nodiscard]] Tab* fallback_after_removal(std::size_t removed_index) const noexcept;
    void activate(Tab* next, bool from_user) noexcept;
    void ensure_active_valid() noexcept;
    [[nodiscard]] bool want_strip() const noexcept;
    void layout() noexcept;
    [[nodiscard]] Limits compute_limits() const noexcept;
    //! Where a page goes: the content area, capped at the page's maximum size.
    [[nodiscard]] RECT child_rect(const Tab& tab) const noexcept;
    void limits_changed() noexcept;
    //! After settings_ changed at run time: clamp, push to the strip, lay out again.
    void apply_settings() noexcept;

    // Window ---------------------------------------------------------------------------------

    //! The messages every container handles the same way. False: not handled.
    bool core_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) noexcept;
    void on_create(HWND wnd) noexcept;
    void on_destroy() noexcept;
    void fill_background(HDC dc) const noexcept;
    //! fill_background, then (Settings::transparent_background) what our parent paints behind us.
    void paint_background(HDC dc) const noexcept;

    // Menus and dialogs ----------------------------------------------------------------------

    void show_tab_menu(std::size_t strip_index, POINT screen, bool with_panel_items, bool with_style) noexcept;
    void append_style_menu(HMENU menu) const noexcept;
    void run_style_command(unsigned command) noexcept;
    //! The Configure dialog. `modeless`: it stays open beside the container (only with a window;
    //! see open_configure_dialog) and this returns false at once. True: OK in the modal dialog.
    bool run_configure(HWND parent, bool modeless);
    //! The modeless Configure dialog is open. Commands that change the tabs are greyed meanwhile:
    //! its OK or Cancel would undo them.
    [[nodiscard]] bool configuring() const noexcept { return configure_wnd_ != nullptr; }
    //! What the strip gets: the settings, without drag reordering while configuring().
    [[nodiscard]] Settings strip_settings() const noexcept;

    // StripListener
    void on_strip_activate(std::size_t index) noexcept override;
    void on_strip_step(int direction) noexcept override;
    void on_strip_menu(std::size_t index, POINT screen) noexcept override;
    void on_strip_overflow(POINT screen) noexcept override;
    bool on_strip_key(UINT message, WPARAM key) noexcept override;
    void on_strip_metrics_changed() noexcept override;
    void on_strip_middle_click(std::size_t index) noexcept override;
    void on_strip_reorder(std::size_t from, std::size_t to) noexcept override;
    void on_strip_reorder_block(std::span<const std::size_t> moved, std::size_t neighbour,
                                bool before) noexcept override;
    void on_strip_pointer() noexcept override;
    void on_strip_paint_failed(const char* detail) noexcept override;
    // HotZoneListener
    void on_hot_zone(bool inside, bool clicked) noexcept override;
    // ConfigureTarget
    void preview(const ConfigureState& state) noexcept override;
    void configure_closed(bool ok, const ConfigureState& state) noexcept override;
    // cover::Listener
    void on_cover_accent_changed() noexcept override;
    void update_cover_subscription() noexcept;

    // Auto-hide --------------------------------------------------------------------------------
    enum class AhTimer : std::uint8_t { none, reveal, hide };
    [[nodiscard]] bool auto_hide() const noexcept { return settings_.visibility == StripVisibility::auto_hide; }
    //! Auto-hide drawn over the panel: chosen, and the strip really is a layered child.
    [[nodiscard]] bool ah_overlay() const noexcept {
        return auto_hide() && settings_.reveal_mode == RevealMode::overlay && strip_.layered();
    }
    //! Creates or drops the hot zone and the layered strip to match the settings.
    void ah_update_mode() noexcept;
    [[nodiscard]] bool ah_pointer_or_pinned() const noexcept;
    //! Decides show/hide from the pointer and the pins; starts or cancels the delay timer.
    void ah_evaluate() noexcept;
    void ah_set_shown(bool shown) noexcept;
    void ah_set_timer(AhTimer kind, unsigned ms) noexcept;
    void ah_on_timer(UINT_PTR id) noexcept;
    //! A tab chosen by the user: the strip shows (if hidden) and stays at least linger_ms.
    void ah_note_switch() noexcept;
    //! Keeps the strip and hot zone above the panels when something got above them: a new panel
    //! window is created on top, and so is one put back with SetParent (a visualisation leaving
    //! its own fullscreen mode). A no-op while they are already the topmost children.
    void ah_raise() noexcept;
    //! Another child window is above the strip or the hot zone (hidden ones included).
    [[nodiscard]] bool ah_covered() const noexcept;
    //! Installs or removes the process-wide reparenting watch: on while any hot zone exists.
    static void ah_sync_parent_watch() noexcept;
    static void CALLBACK ah_on_parent_change(HWINEVENTHOOK hook, DWORD event, HWND wnd, LONG object, LONG child,
                                             DWORD thread, DWORD time) noexcept;
    [[nodiscard]] int ah_px(unsigned dip) const noexcept;

    Settings settings_{};
    std::vector<RawField> unknown_settings_;
    std::vector<RawField> unknown_sections_;
    std::vector<std::unique_ptr<Tab>> tabs_;
    Tab* active_{nullptr};
    //! The active index from the layout, used when the window is created.
    std::uint32_t saved_active_{0};

    StripWindow strip_;
    std::vector<std::size_t> visible_; // strip index -> tabs_ index
    //! The tabs of a multiple selection while the tab menu is open.
    std::vector<Tab*> selected_for_menu_;
    std::vector<StripItem> items_;
    //! The tabs as the open Configure dialog numbers them (TabEdit::id).
    std::vector<Tab*> config_tabs_;
    //! Time spent creating panel windows, for the performance log.
    double child_ms_{0.0};
    bool strip_shown_{false};
    RECT content_{};
    Limits limits_{};
    COLORREF background_{RGB(255, 255, 255)};
    //! Fill for transparent children (HostColours::layout).
    COLORREF child_background_{RGB(255, 255, 255)};
    bool in_create_{false};
    bool cover_subscribed_{false};
    //! The Settings::font the strip's font was last built with; refresh_font() when it differs.
    TabFont applied_font_{};

    // Auto-hide state.
    HotZone hot_zone_;
    bool ah_shown_{false};
    AhTimer ah_timer_{AhTimer::none};
    //! A menu (or a dialog from it) of this strip is open.
    bool menu_pin_{false};
    //! The modeless Configure dialog, and what Cancel puts back. It pins the strip too.
    HWND configure_wnd_{nullptr};
    ConfigureState configure_original_;
    ULONGLONG linger_until_{0};
    //! Show/hide animation: progress 0 (hidden) to 1 (shown), heading for ah_shown_.
    bool ah_animating_{false};
    float ah_progress_{0.0f};
    float ah_from_{0.0f};
    ULONGLONG ah_anim_start_{0};
};

} // namespace bettertabs
