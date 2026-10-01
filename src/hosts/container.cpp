// The Columns UI container: a uie::splitter_window_v3 that shows one child at a time under a tab
// strip. It owns the children and the strip window; the strip owns all drawing.
//
// Performance shape (PLAN.md 5):
//  - a child's window is created the first time its tab is shown (lazy_children), never before;
//  - a switch is one DeferWindowPos batch: show the new child (moved only if its rectangle went
//    stale), hide the old one. Nothing else is touched and nothing is invalidated;
//  - a resize moves the strip and the active child only; hidden children catch up when shown;
//  - no timers, hooks or polling while idle.
//
// Behaviour follows Columns UI's own Tab stack wherever the SDK leaves room for choice
// (foo_ui_columns/splitter_tabs.cpp): host semantics, missing panels, config items, export.

#include <helpers/foobar2000+atl.h>

#include <columns_ui-sdk/ui_extension.h>

#include <commdlg.h>
#include <windowsx.h>

#pragma comment(lib, "comdlg32.lib")

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <vector>

#include "../guids.h"
#include "../model/codec.h"
#include "../model/colour.h"
#include "../model/settings.h"
#include "../platform/cover_hub.h"
#include "../platform/graphics.h"
#include "../platform/logging.h"
#include "../platform/perf.h"
#include "../strip/strip_window.h"
#include "../version.h"

namespace bettertabs {

namespace {

class TabsContainer;

//! Live containers, for the colour and font clients (singletons with const callbacks). Main thread.
std::vector<TabsContainer*>& live_containers() {
    static std::vector<TabsContainer*> list;
    return list;
}

constexpr unsigned menu_tab_base = 1;
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
    style_chip,
    style_accent_selection,
    style_accent_cover,
    style_accent_custom,
    style_sizing_fit,
    style_sizing_equal,
    style_sizing_fill,
    style_align_start,
    style_align_centre,
    style_align_end,
    style_show_always,
    style_show_two_or_more,
    style_last,
};
constexpr LONG limit_cap = MAXSHORT;

[[nodiscard]] Bytes to_bytes(const pfc::array_t<t_uint8>& data) {
    return Bytes(data.get_ptr(), data.get_ptr() + data.get_size());
}

[[nodiscard]] Bytes read_all(stream_reader* reader, t_size size, abort_callback& abort) {
    Bytes bytes(size);
    if (size != 0) reader->read_object(bytes.data(), size, abort);
    return bytes;
}

[[nodiscard]] std::wstring widen(const char* utf8) {
    const pfc::stringcvt::string_wide_from_utf8 wide(utf8);
    return std::wstring(wide.get_ptr());
}

[[nodiscard]] pfc::string8 narrow(const std::wstring& wide) {
    return pfc::string8(pfc::stringcvt::string_utf8_from_wide(wide.c_str()).get_ptr());
}

//! Menu text: a lone '&' would underline the next letter.
[[nodiscard]] std::wstring menu_text(const std::wstring& label) {
    std::wstring out;
    out.reserve(label.size() + 4);
    for (const wchar_t c : label) {
        if (c == L'&') out += L'&';
        out += c;
    }
    return out;
}

[[nodiscard]] bool same_rect(const RECT& a, const RECT& b) noexcept {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

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

struct Tab {
    GUID guid{};
    //! The child's config as last read from it (or from the layout, before it existed).
    Bytes config;
    TabExtra extra;
    //! The extension object. Exists as soon as the container does; its window only when shown.
    uie::window_ptr window;
    bool object_tried{false};
    HWND wnd{nullptr};
    //! The rectangle last given to wnd; a hidden child keeps a stale one until it is shown.
    RECT applied{};
    uie::size_limit_t limits{};
    std::wstring label;
};

class TabsHost;

class TabsContainer : public uie::container_uie_window_v3_t<uie::splitter_window_v3>,
                      private StripListener,
                      private cover::Listener {
public:
    TabsContainer() { live_containers().push_back(this); }
    ~TabsContainer() { std::erase(live_containers(), this); }
    TabsContainer(const TabsContainer&) = delete;
    TabsContainer& operator=(const TabsContainer&) = delete;

    // uie::extension_base / uie::window ------------------------------------------------------

    const GUID& get_extension_guid() const override { return guids::container; }
    void get_name(pfc::string_base& out) const override { out = BETTERTABS_NAME; }
    void get_category(pfc::string_base& out) const override { out = "Splitters"; }
    bool get_description(pfc::string_base& out) const override {
        out = "Shows one panel at a time under a fast, modern tab strip.";
        return true;
    }
    unsigned get_type() const override { return uie::type_layout | uie::type_splitter; }

    void set_config(stream_reader* reader, t_size size, abort_callback& abort) override;
    void get_config(stream_writer* writer, abort_callback& abort) const override;
    void import_config(stream_reader* reader, t_size size, abort_callback& abort) override;
    void export_config(stream_writer* writer, abort_callback& abort) const override;

    uie::container_window_v3_config get_window_config() override {
        // Not transparent: that would repaint the whole container on every move and resize.
        return {L"foo_bettertabs_container", false};
    }
    LRESULT on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) override;

    // uie::splitter_window ----------------------------------------------------------------

    void insert_panel(t_size index, const uie::splitter_item_t* item) override;
    void remove_panel(t_size index) override;
    void replace_panel(t_size index, const uie::splitter_item_t* item) override;
    t_size get_panel_count() const override { return tabs_.size(); }
    bool get_config_item_supported(t_size index, const GUID& type) const override;
    bool get_config_item(t_size index, const GUID& type, stream_writer* out, abort_callback& abort) const override;
    bool set_config_item(t_size index, const GUID& type, stream_reader* source, abort_callback& abort) override;
    void get_supported_panels(const pfc::list_base_const_t<uie::window::ptr>& windows,
                              bit_array_var& mask_unsupported) override;
    void reorder_panels(const size_t* order, size_t count) override;
    //! Layout editing (live edit, Ctrl+Shift+right-click) walks the tree through this. Without it the
    //! container cannot be selected and its children's menus lack the container entries.
    bool is_point_ours(HWND wnd_point, const POINT& pt_screen, pfc::list_base_t<uie::window_ptr>& hierarchy) override;

    // For the host and the colour/font clients ---------------------------------------------

    [[nodiscard]] Tab* find_by_wnd(HWND wnd) const noexcept;
    void on_child_limits_changed(HWND wnd) noexcept;
    bool show_child(HWND wnd) noexcept;
    void relinquish(HWND wnd) noexcept;
    void get_children(pfc::list_base_t<uie::window_ptr>& out) const;
    [[nodiscard]] bool self_visible() const;
    void refresh_appearance() noexcept;
    void refresh_colours() noexcept;
    void refresh_font() noexcept;

protected:
    uie::splitter_item_t* get_panel(t_size index) const override;

private:
    // StripListener
    void on_strip_activate(std::size_t index) noexcept override;
    void on_strip_step(int direction) noexcept override;
    void on_strip_menu(std::size_t index, POINT screen) noexcept override;
    void on_strip_overflow(POINT screen) noexcept override;
    bool on_strip_key(UINT message, WPARAM key) noexcept override;
    void on_strip_metrics_changed() noexcept override;
    // cover::Listener
    void on_cover_accent_changed() noexcept override;
    void update_cover_subscription() noexcept;
    //! After settings_ changed at run time: clamp, push to the strip, lay out again.
    void apply_settings() noexcept;
    void append_style_menu(HMENU menu) const noexcept;
    void run_style_command(unsigned command) noexcept;

    void on_create(HWND wnd) noexcept;
    void on_destroy() noexcept;
    void on_paint(HWND wnd) noexcept;
    void fill_background(HDC dc) const noexcept;

    void load(InstanceData&& data);
    [[nodiscard]] InstanceData snapshot(bool refresh_children) const;
    [[nodiscard]] std::unique_ptr<Tab> tab_from_item(const uie::splitter_item_t* item) const;
    [[nodiscard]] uie::window_host_ptr host_for_availability() const;

    bool ensure_object(Tab& tab) noexcept;
    bool ensure_window(Tab& tab) noexcept;
    void destroy_tab_window(Tab& tab) noexcept;
    void update_label(Tab& tab) noexcept;
    [[nodiscard]] bool tab_visible(const Tab& tab) const noexcept;

    void rebuild_strip() noexcept;
    [[nodiscard]] std::size_t strip_index_of(const Tab* tab) const noexcept;
    [[nodiscard]] std::size_t index_of(const Tab* tab) const noexcept;
    [[nodiscard]] Tab* fallback_after_removal(std::size_t removed_index) const noexcept;
    void activate(Tab* next, bool from_user) noexcept;
    void ensure_active_valid() noexcept;

    [[nodiscard]] bool want_strip() const noexcept;
    void layout() noexcept;
    [[nodiscard]] uie::size_limit_t compute_limits() const noexcept;
    void limits_changed() noexcept;
    static void query_limits(Tab& tab) noexcept;

    void show_tab_menu(std::size_t strip_index, POINT screen, bool with_panel_items, bool with_style) noexcept;

    Settings settings_{};
    std::vector<RawField> unknown_settings_;
    std::vector<RawField> unknown_sections_;
    std::vector<std::unique_ptr<Tab>> tabs_;
    Tab* active_{nullptr};
    //! The active index from the layout, used when the window is created.
    std::uint32_t saved_active_{0};

    service_ptr_t<TabsHost> host_;
    StripWindow strip_;
    std::vector<std::size_t> visible_; // strip index -> tabs_ index
    std::vector<std::wstring> labels_;
    bool strip_shown_{false};
    RECT content_{};
    uie::size_limit_t limits_{};
    COLORREF background_{RGB(255, 255, 255)};
    bool in_create_{false};
    bool cover_subscribed_{false};
};

// ---------------------------------------------------------------------------------------------
// The host handed to children. Null-safe: after the container's window is gone it answers
// "no" to everything, and the default-constructed instance registered below does the same.

class TabsHost : public uie::window_host_ex {
public:
    TabsHost() = default;
    explicit TabsHost(TabsContainer* owner) noexcept : owner_(owner) {}
    void detach() noexcept { owner_ = nullptr; }

    const GUID& get_host_guid() const override { return guids::host; }

    bool get_keyboard_shortcuts_enabled() const override {
        if (owner_ == nullptr) return true;
        const auto& parent = owner_->get_host();
        return !parent.is_valid() || parent->get_keyboard_shortcuts_enabled();
    }

    void on_size_limit_change(HWND wnd, unsigned) override {
        if (owner_ != nullptr) owner_->on_child_limits_changed(wnd);
    }

    unsigned is_resize_supported(HWND) const override { return 0; }
    // Like Tab stack: a tab cannot resize the container on a child's behalf.
    bool request_resize(HWND, unsigned, unsigned, unsigned) override { return false; }

    bool override_status_text_create(service_ptr_t<ui_status_text_override>& out) override {
        if (owner_ == nullptr) return false;
        const auto& parent = owner_->get_host();
        return parent.is_valid() && parent->override_status_text_create(out);
    }

    bool is_visible(HWND wnd) const override {
        return owner_ != nullptr && owner_->self_visible() && IsWindowVisible(wnd) != FALSE;
    }

    bool is_visibility_modifiable(HWND wnd, bool desired_visibility) const override {
        if (owner_ == nullptr || !desired_visibility || owner_->find_by_wnd(wnd) == nullptr) return false;
        if (owner_->self_visible()) return true;
        const auto& parent = owner_->get_host();
        return parent.is_valid() && parent->is_visibility_modifiable(owner_->get_wnd(), true);
    }

    bool set_window_visibility(HWND wnd, bool visibility) override {
        if (owner_ == nullptr || !visibility) return false;
        if (!owner_->self_visible()) {
            const auto& parent = owner_->get_host();
            if (!parent.is_valid() || !parent->set_window_visibility(owner_->get_wnd(), true)) return false;
        }
        return owner_->show_child(wnd);
    }

    void relinquish_ownership(HWND wnd) override {
        if (owner_ != nullptr) owner_->relinquish(wnd);
    }

    void get_children(pfc::list_base_t<uie::window_ptr>& out) override {
        if (owner_ != nullptr) owner_->get_children(out);
    }

private:
    TabsContainer* owner_{nullptr};
};

uie::window_host_factory<TabsHost> g_host_factory;

// ---------------------------------------------------------------------------------------------
// Configuration.

void TabsContainer::load(InstanceData&& data) {
    settings_ = data.settings;
    unknown_settings_ = std::move(data.unknown_settings);
    unknown_sections_ = std::move(data.unknown_sections);
    saved_active_ = data.active;
    tabs_.clear();
    tabs_.reserve(data.children.size());
    for (ChildRecord& child : data.children) {
        auto tab = std::make_unique<Tab>();
        tab->guid = child.guid;
        tab->config = std::move(child.config);
        tab->extra = decode_tab_extra(child.extra);
        tabs_.push_back(std::move(tab));
    }
}

InstanceData TabsContainer::snapshot(bool refresh_children) const {
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
        if (refresh_children && tab->wnd != nullptr && tab->window.is_valid()) {
            try {
                pfc::array_t<t_uint8> live;
                tab->window->get_config_to_array(live, fb2k::noAbort, true);
                child.config = to_bytes(live);
            } catch (const std::exception& e) {
                log::warn(std::string("could not read a panel's settings: ") + e.what());
            }
        }
        child.extra = encode_tab_extra(tab->extra);
        data.children.push_back(std::move(child));
    }
    return data;
}

void TabsContainer::set_config(stream_reader* reader, t_size size, abort_callback& abort) {
    InstanceData data;
    try {
        data = decode_instance(read_all(reader, size, abort));
    } catch (const std::exception& e) {
        // A damaged blob must not take the whole layout down: start empty with defaults.
        log::warn(std::string("could not read settings, using defaults: ") + e.what());
        data = InstanceData{};
    }
    const bool live = get_wnd() != nullptr;
    if (live) {
        for (auto& tab : tabs_) destroy_tab_window(*tab);
        active_ = nullptr;
    }
    load(std::move(data));
    if (live) {
        for (auto& tab : tabs_) ensure_object(*tab);
        strip_.set_settings(settings_);
        update_cover_subscription();
        refresh_colours();
        rebuild_strip();
        ensure_active_valid();
        layout();
        limits_changed();
    }
}

void TabsContainer::get_config(stream_writer* writer, abort_callback& abort) const {
    const Bytes bytes = encode_instance(snapshot(true));
    writer->write(bytes.data(), bytes.size(), abort);
}

void TabsContainer::export_config(stream_writer* writer, abort_callback& abort) const {
    InstanceData data = snapshot(true);
    for (std::size_t i = 0; i < tabs_.size(); ++i) {
        uie::window_ptr child = tabs_[i]->window;
        if (!child.is_valid()) {
            // Tab stack does the same: an FCL that silently dropped a panel would be worse.
            if (!uie::window::create_by_guid(tabs_[i]->guid, child)) throw cui::fcl::exception_missing_panel();
            try {
                child->set_config_from_ptr(data.children[i].config.data(), data.children[i].config.size(), abort);
            } catch (const exception_io&) {
            }
        }
        pfc::array_t<t_uint8> exported;
        child->export_config_to_array(exported, abort, true);
        data.children[i].config = to_bytes(exported);
    }
    const Bytes bytes = encode_instance(data);
    writer->write(bytes.data(), bytes.size(), abort);
}

void TabsContainer::import_config(stream_reader* reader, t_size size, abort_callback& abort) {
    InstanceData data = decode_instance(read_all(reader, size, abort));
    std::vector<uie::window_ptr> objects(data.children.size());
    for (std::size_t i = 0; i < data.children.size(); ++i) {
        ChildRecord& child = data.children[i];
        uie::window_ptr object;
        if (!uie::window::create_by_guid(child.guid, object)) {
            child.config.clear();
            continue;
        }
        try {
            object->import_config_from_ptr(child.config.data(), child.config.size(), abort);
        } catch (const exception_io&) {
        }
        pfc::array_t<t_uint8> config;
        object->get_config_to_array(config, abort, true);
        child.config = to_bytes(config);
        objects[i] = object;
    }
    load(std::move(data));
    for (std::size_t i = 0; i < tabs_.size() && i < objects.size(); ++i) {
        if (objects[i].is_valid()) tabs_[i]->window = objects[i];
    }
}

// ---------------------------------------------------------------------------------------------
// Children.

uie::window_host_ptr TabsContainer::host_for_availability() const {
    if (host_.is_valid()) return host_;
    // Ownerless: a panel that kept this host from is_available() could never reach a dead container.
    return fb2k::service_new<TabsHost>();
}

bool TabsContainer::ensure_object(Tab& tab) noexcept {
    if (tab.window.is_valid()) return true;
    if (tab.object_tried) return false;
    tab.object_tried = true;
    try {
        uie::window_ptr object;
        if (!uie::window::create_by_guid(tab.guid, object)) return false;
        if (!object->is_available(host_for_availability())) return false;
        try {
            object->set_config_from_ptr(tab.config.data(), tab.config.size(), fb2k::noAbort);
        } catch (const exception_io& e) {
            log::warn(std::string("a panel rejected its settings: ") + e.what());
        }
        tab.window = object;
        update_label(tab);
        return true;
    } catch (const std::exception& e) {
        log::warn(std::string("could not create a panel: ") + e.what());
        tab.window.release();
        return false;
    }
}

void TabsContainer::query_limits(Tab& tab) noexcept {
    MINMAXINFO mmi{};
    mmi.ptMaxTrackSize.x = MAXLONG;
    mmi.ptMaxTrackSize.y = MAXLONG;
    SendMessageW(tab.wnd, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&mmi));
    tab.limits.min_width = static_cast<unsigned>(std::clamp(mmi.ptMinTrackSize.x, 0L, limit_cap));
    tab.limits.min_height = static_cast<unsigned>(std::clamp(mmi.ptMinTrackSize.y, 0L, limit_cap));
    tab.limits.max_width = static_cast<unsigned>(std::clamp(mmi.ptMaxTrackSize.x, 0L, limit_cap));
    tab.limits.max_height = static_cast<unsigned>(std::clamp(mmi.ptMaxTrackSize.y, 0L, limit_cap));
}

bool TabsContainer::ensure_window(Tab& tab) noexcept {
    if (tab.wnd != nullptr) return true;
    const HWND self = get_wnd();
    if (self == nullptr || !ensure_object(tab)) return false;
    try {
        const ui_helpers::window_position_t position(content_);
        const HWND wnd = tab.window->create_or_transfer_window(self, host_, position);
        if (wnd == nullptr) return false;
        if ((GetWindowLongPtrW(wnd, GWL_STYLE) & WS_VISIBLE) != 0) {
            // Same repair as Tab stack; a panel should be created hidden.
            ShowWindow(wnd, SW_HIDE);
        }
        SetWindowLongPtrW(wnd, GWL_STYLE, GetWindowLongPtrW(wnd, GWL_STYLE) | WS_CLIPSIBLINGS);
        tab.wnd = wnd;
        tab.applied = content_;
        query_limits(tab);
        update_label(tab);
        return true;
    } catch (const std::exception& e) {
        log::warn(std::string("could not create a panel window: ") + e.what());
        return false;
    }
}

void TabsContainer::destroy_tab_window(Tab& tab) noexcept {
    if (tab.wnd != nullptr && tab.window.is_valid()) {
        try {
            pfc::array_t<t_uint8> live;
            tab.window->get_config_to_array(live, fb2k::noAbort, true);
            tab.config = to_bytes(live);
        } catch (...) {
        }
        try {
            tab.window->destroy_window();
        } catch (...) {
        }
    }
    tab.wnd = nullptr;
    tab.window.release();
    tab.object_tried = false;
    tab.applied = RECT{};
}

void TabsContainer::update_label(Tab& tab) noexcept {
    try {
        if (tab.extra.use_custom_title && !tab.extra.title.empty()) {
            tab.label = widen(tab.extra.title.c_str());
            return;
        }
        pfc::string8 name;
        if (tab.window.is_valid()) {
            if (!tab.window->get_short_name(name)) tab.window->get_name(name);
        }
        tab.label = widen(name.get_ptr());
    } catch (...) {
        tab.label.clear();
    }
}

bool TabsContainer::tab_visible(const Tab& tab) const noexcept {
    return !tab.extra.hidden && tab.window.is_valid();
}

Tab* TabsContainer::find_by_wnd(HWND wnd) const noexcept {
    if (wnd == nullptr) return nullptr;
    for (const auto& tab : tabs_) {
        if (tab->wnd == wnd) return tab.get();
    }
    return nullptr;
}

std::size_t TabsContainer::index_of(const Tab* tab) const noexcept {
    if (tab == nullptr) return no_index;
    for (std::size_t i = 0; i < tabs_.size(); ++i) {
        if (tabs_[i].get() == tab) return i;
    }
    return no_index;
}

std::size_t TabsContainer::strip_index_of(const Tab* tab) const noexcept {
    const std::size_t index = index_of(tab);
    if (index == no_index) return no_index;
    for (std::size_t s = 0; s < visible_.size(); ++s) {
        if (visible_[s] == index) return s;
    }
    return no_index;
}

void TabsContainer::get_children(pfc::list_base_t<uie::window_ptr>& out) const {
    for (const auto& tab : tabs_) {
        if (tab->window.is_valid()) out.add_item(tab->window);
    }
}

bool TabsContainer::self_visible() const {
    const HWND self = get_wnd();
    if (self == nullptr) return false;
    const auto& parent = get_host();
    return parent.is_valid() ? parent->is_visible(self) : IsWindowVisible(self) != FALSE;
}

// ---------------------------------------------------------------------------------------------
// Tabs and switching.

void TabsContainer::rebuild_strip() noexcept {
    try {
        visible_.clear();
        labels_.clear();
        for (std::size_t i = 0; i < tabs_.size(); ++i) {
            if (!tab_visible(*tabs_[i])) continue;
            visible_.push_back(i);
            labels_.push_back(tabs_[i]->label);
        }
    } catch (...) {
        visible_.clear();
        labels_.clear();
    }
    strip_.set_labels(labels_, strip_index_of(active_));
}

Tab* TabsContainer::fallback_after_removal(std::size_t removed_index) const noexcept {
    // The next visible tab, else the previous one (Tab stack picks the neighbour the same way).
    for (std::size_t i = removed_index; i < tabs_.size(); ++i) {
        if (tab_visible(*tabs_[i])) return tabs_[i].get();
    }
    for (std::size_t i = (std::min)(removed_index, tabs_.size()); i > 0; --i) {
        if (tab_visible(*tabs_[i - 1])) return tabs_[i - 1].get();
    }
    return nullptr;
}

void TabsContainer::ensure_active_valid() noexcept {
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

void TabsContainer::activate(Tab* next, bool from_user) noexcept {
    const HWND self = get_wnd();
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

    WindowMoves moves;
    if (next != nullptr && next->wnd != nullptr) {
        // Show first, then hide: the parent's background is never exposed in between.
        UINT flags = SWP_SHOWWINDOW;
        if (same_rect(next->applied, content_)) flags |= SWP_NOMOVE | SWP_NOSIZE;
        next->applied = content_;
        moves.add(next->wnd, content_, flags);
    }
    if (old != nullptr && old != next && old->wnd != nullptr) {
        moves.add(old->wnd, old->applied, SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE);
    }
    active_ = next;
    moves.apply();

    if (setredraw) {
        SendMessageW(self, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(self, &content_, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
    }
    strip_.set_active(strip_index_of(next));

    if (created) limits_changed();

    if (next != nullptr && next->wnd != nullptr && (focus_in_old || (from_user && focus == self))) {
        HWND target = next->wnd;
        if ((GetWindowLongPtrW(target, GWL_STYLE) & WS_TABSTOP) == 0) {
            target = GetNextDlgTabItem(next->wnd, next->wnd, FALSE);
        }
        const bool ok = target != nullptr && (target == next->wnd || IsChild(next->wnd, target)) &&
                        (GetWindowLongPtrW(target, GWL_STYLE) & WS_TABSTOP) != 0;
        SetFocus(ok ? target : strip_.hwnd());
    }

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
              << pfc::format_float(strip_stats.worst_ms, 0, 3) << " ms, " << pfc::format_uint(strip_stats.pixels)
              << " px, " << pfc::format_uint(strip_stats.allocating_paints) << " allocating";
        }
        if (setredraw) f << " [WM_SETREDRAW]";
        log::info(f.get_ptr());
    }
}

bool TabsContainer::show_child(HWND wnd) noexcept {
    Tab* tab = find_by_wnd(wnd);
    if (tab == nullptr || !tab_visible(*tab)) return false;
    activate(tab, false);
    return active_ == tab;
}

void TabsContainer::relinquish(HWND wnd) noexcept {
    // The child moved to another host: forget it without destroying it.
    Tab* tab = find_by_wnd(wnd);
    if (tab == nullptr) return;
    const std::size_t index = index_of(tab);
    const bool was_active = tab == active_;
    if (was_active) active_ = nullptr;
    tab->wnd = nullptr;
    tab->window.release();
    tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(index));
    rebuild_strip();
    if (was_active) activate(fallback_after_removal(index), false);
    layout();
    limits_changed();
}

// ---------------------------------------------------------------------------------------------
// Layout and size limits.

bool TabsContainer::want_strip() const noexcept {
    switch (settings_.visibility) {
    case StripVisibility::never: return false;
    case StripVisibility::two_or_more: return visible_.size() >= 2;
    case StripVisibility::always:
    case StripVisibility::auto_hide: // M(d); until then, always shown
    default: return true;
    }
}

void TabsContainer::layout() noexcept {
    const HWND self = get_wnd();
    if (self == nullptr) return;
    RECT client{};
    GetClientRect(self, &client);
    const bool show = want_strip() && strip_.hwnd() != nullptr;
    RECT strip_rc{};
    content_ = client;
    if (show) {
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

    WindowMoves moves;
    if (strip_.hwnd() != nullptr) {
        if (show) {
            moves.add(strip_.hwnd(), strip_rc, SWP_SHOWWINDOW);
        } else if (strip_shown_) {
            moves.add(strip_.hwnd(), strip_rc, SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE);
        }
    }
    if (active_ != nullptr && active_->wnd != nullptr && !same_rect(active_->applied, content_)) {
        active_->applied = content_;
        moves.add(active_->wnd, content_, 0);
    }
    moves.apply();

    if (show != strip_shown_) {
        strip_shown_ = show;
        limits_changed();
    }
}

uie::size_limit_t TabsContainer::compute_limits() const noexcept {
    uie::size_limit_t out;
    out.min_width = 0;
    out.min_height = 0;
    out.max_width = limit_cap;
    out.max_height = limit_cap;
    for (const auto& tab : tabs_) {
        if (tab->wnd == nullptr) continue;
        out.min_width = (std::max)(out.min_width, tab->limits.min_width);
        out.min_height = (std::max)(out.min_height, tab->limits.min_height);
        out.max_width = (std::min)(out.max_width, tab->limits.max_width);
        out.max_height = (std::min)(out.max_height, tab->limits.max_height);
    }
    out.max_width = (std::max)(out.max_width, out.min_width);
    out.max_height = (std::max)(out.max_height, out.min_height);
    if (strip_shown_) {
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

void TabsContainer::limits_changed() noexcept {
    const uie::size_limit_t next = compute_limits();
    const bool same = next.min_width == limits_.min_width && next.min_height == limits_.min_height &&
                      next.max_width == limits_.max_width && next.max_height == limits_.max_height;
    limits_ = next;
    if (same || in_create_) return;
    const HWND self = get_wnd();
    const auto& parent = get_host();
    if (self == nullptr || !parent.is_valid()) return;
    try {
        parent->on_size_limit_change(self, uie::size_limit_all);
    } catch (...) {
    }
}

void TabsContainer::on_child_limits_changed(HWND wnd) noexcept {
    Tab* tab = find_by_wnd(wnd);
    if (tab == nullptr) return;
    query_limits(*tab);
    limits_changed();
    layout();
}

// ---------------------------------------------------------------------------------------------
// Window.

LRESULT TabsContainer::on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    try {
        switch (msg) {
        case WM_CREATE: on_create(wnd); return 0;
        case WM_DESTROY: on_destroy(); return 0;
        case WM_SIZE: layout(); return 0;
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = static_cast<LONG>(limits_.min_width);
            mmi->ptMinTrackSize.y = static_cast<LONG>(limits_.min_height);
            mmi->ptMaxTrackSize.x = static_cast<LONG>(limits_.max_width);
            mmi->ptMaxTrackSize.y = static_cast<LONG>(limits_.max_height);
            return 0;
        }
        // Transparent children (Columns UI splitters, toolbars, many panels) paint their
        // background by forwarding these to us with their own DC and origin, so both must really
        // paint: an empty Row/Column splitter would otherwise keep whatever was on screen before.
        // Our own erase only reaches the area no child covers (WS_CLIPCHILDREN).
        case WM_ERASEBKGND: fill_background(reinterpret_cast<HDC>(wp)); return 1;
        case WM_PRINTCLIENT:
            if ((lp & PRF_ERASEBKGND) != 0) fill_background(reinterpret_cast<HDC>(wp));
            return 0;
        case WM_PAINT: on_paint(wnd); return 0;
        case WM_SETFOCUS:
            if (active_ != nullptr && active_->wnd != nullptr) {
                SetFocus(active_->wnd);
            } else if (strip_.hwnd() != nullptr && strip_shown_) {
                SetFocus(strip_.hwnd());
            }
            return 0;
        default: break;
        }
    } catch (const std::exception& e) {
        log::warn(std::string("container message failed: ") + e.what());
    } catch (...) {
        log::warn("container message failed");
    }
    return DefWindowProc(wnd, msg, wp, lp);
}

void TabsContainer::on_create(HWND wnd) noexcept {
    const bool measure = perf::enabled();
    const std::uint64_t t_start = measure ? perf::now() : 0;
    in_create_ = true;
    try {
        host_ = fb2k::service_new<TabsHost>(this);
        if (!strip_.create(wnd, *this)) log::warn("could not create the tab strip");
        strip_.set_settings(settings_);
        update_cover_subscription();
        refresh_appearance();
        GetClientRect(wnd, &content_);
        for (auto& tab : tabs_) ensure_object(*tab);
        rebuild_strip();
        layout();
        if (!settings_.lazy_children) {
            for (auto& tab : tabs_) {
                if (tab_visible(*tab)) ensure_window(*tab);
            }
        }
        ensure_active_valid();
        layout();
    } catch (const std::exception& e) {
        log::warn(std::string("could not set up the container: ") + e.what());
    }
    in_create_ = false;
    limits_ = compute_limits();

    if (measure) {
        std::size_t created = 0;
        for (const auto& tab : tabs_) created += tab->wnd != nullptr ? 1 : 0;
        pfc::string_formatter f;
        f << "container created with " << pfc::format_uint(tabs_.size()) << " tabs (" << pfc::format_uint(created)
          << " panel windows) in "
          << pfc::format_float(perf::elapsed_ms(t_start, perf::now()), 0, 3) << " ms";
        log::info(f.get_ptr());
    }
}

void TabsContainer::on_destroy() noexcept {
    const std::size_t active = index_of(active_);
    if (active != no_index) saved_active_ = static_cast<std::uint32_t>(active);
    for (auto& tab : tabs_) destroy_tab_window(*tab);
    active_ = nullptr;
    strip_.destroy();
    strip_shown_ = false;
    if (cover_subscribed_) {
        cover::unsubscribe(this);
        cover_subscribed_ = false;
    }
    visible_.clear();
    if (host_.is_valid()) host_->detach();
    host_.release();
}

void TabsContainer::on_paint(HWND wnd) noexcept {
    // Only reached where no child covers the client area (WS_CLIPCHILDREN), e.g. with no tabs.
    PAINTSTRUCT ps{};
    const HDC dc = BeginPaint(wnd, &ps);
    if (dc != nullptr) fill_background(dc);
    EndPaint(wnd, &ps);
}

void TabsContainer::fill_background(HDC dc) const noexcept {
    if (dc == nullptr) return;
    RECT clip{};
    if (GetClipBox(dc, &clip) == ERROR || IsRectEmpty(&clip)) return;
    // The stock DC brush: no GDI object is created per erase.
    const COLORREF previous = SetDCBrushColor(dc, background_);
    FillRect(dc, &clip, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SetDCBrushColor(dc, previous);
}

void TabsContainer::refresh_appearance() noexcept {
    refresh_colours();
    refresh_font();
}

void TabsContainer::refresh_colours() noexcept {
    try {
        const cui::colours::helper colours(guids::colour_client);
        StripTheme theme;
        theme.background = colours.get_colour(cui::colours::colour_background);
        theme.text = colours.get_colour(cui::colours::colour_text);
        theme.dark = colours.is_dark_mode_active();

        // Every accent passes a 3:1 contrast floor against the strip. A cover colour is raw, so
        // it also gets the full legibility treatment (lightness window, chroma floor; same as
        // Media Bar). Columns UI's selection colour and a custom colour are the user's choice
        // and are only nudged when they would vanish.
        const std::uint32_t bg = colour::rgb_from_colorref(theme.background);
        std::uint32_t accent = colour::rgb_from_colorref(colours.get_colour(cui::colours::colour_selection_background));
        if (settings_.accent_source == AccentSource::custom) accent = settings_.accent_argb & 0xFFFFFFu;
        if (settings_.accent_source == AccentSource::cover) {
            if (const auto raw = cover::current(); raw) {
                accent = colour::accent_for_background(*raw, bg);
            }
        }
        theme.accent = colour::colorref_from_rgb(colour::with_min_contrast(accent, bg, colour::accent_min_contrast));

        background_ = theme.background;
        strip_.set_theme(theme);
        if (const HWND self = get_wnd(); self != nullptr) InvalidateRect(self, nullptr, FALSE);
    } catch (...) {
    }
}

void TabsContainer::refresh_font() noexcept {
    try {
        StripFont font;
        font.font = cui::fonts::get_log_font_with_fallback(guids::font_client);
        font.font_dpi = gfx::system_dpi();
        StripTextOptions options;
        cui::fonts::rendering_options::ptr rendering;
        try {
            // Columns UI 3+: the DirectWrite font, its emoji fallback and the text rendering
            // options. Earlier versions have no manager_v3 and leave `font.family` empty.
            if (const cui::fonts::font::ptr dw = cui::fonts::get_font(guids::font_client); dw.is_valid()) {
                if (const wchar_t* family = dw->family_name(); family != nullptr) font.family = family;
                font.weight = dw->weight();
                font.style = dw->style();
                font.stretch = dw->stretch();
                font.size_dip = dw->size();
                IDWriteFontFallback* fallback = nullptr;
                if (SUCCEEDED(dw->create_font_fallback(&fallback)) && fallback != nullptr) {
                    *font.fallback.put() = fallback;
                }
                rendering = dw->rendering_options();
            }
        } catch (...) {
            font.family.clear();
        }
        if (rendering.is_valid()) {
            options.antialias = rendering->rendering_mode() == DWRITE_RENDERING_MODE_ALIASED ? TextAntialias::aliased
                                : rendering->use_greyscale_antialiasing()                    ? TextAntialias::greyscale
                                                                                             : TextAntialias::automatic;
            options.gdi_compatible = rendering->use_gdi_compatible_layout();
            options.gdi_natural = rendering->use_gdi_natural();
            options.colour_glyphs = rendering->use_colour_glyphs();
            const HWND self = get_wnd();
            if (IDWriteFactory* factory = gfx::dwrite(); factory != nullptr && self != nullptr) {
                IDWriteRenderingParams* params = nullptr;
                if (SUCCEEDED(rendering->create_rendering_params(
                        factory, MonitorFromWindow(self, MONITOR_DEFAULTTONEAREST), &params)) &&
                    params != nullptr) {
                    *options.params.put() = params;
                }
            }
        }

        const int before = strip_.thickness();
        strip_.set_text_options(options);
        strip_.set_font(font);
        if (strip_.thickness() != before && get_wnd() != nullptr) {
            layout();
            limits_changed();
        }
    } catch (...) {
    }
}

void TabsContainer::on_cover_accent_changed() noexcept { refresh_colours(); }

void TabsContainer::update_cover_subscription() noexcept {
    const bool want = get_wnd() != nullptr && settings_.accent_source == AccentSource::cover;
    if (want == cover_subscribed_) return;
    cover_subscribed_ = want;
    if (want) {
        cover::subscribe(this);
    } else {
        cover::unsubscribe(this);
    }
}

void TabsContainer::apply_settings() noexcept {
    clamp(settings_);
    strip_.set_settings(settings_);
    update_cover_subscription();
    refresh_colours();
    layout();
    limits_changed();
}

// ---------------------------------------------------------------------------------------------
// splitter_window.

std::unique_ptr<Tab> TabsContainer::tab_from_item(const uie::splitter_item_t* item) const {
    auto tab = std::make_unique<Tab>();
    tab->guid = item->get_panel_guid();
    pfc::array_t<t_uint8> config;
    item->get_panel_config_to_array(config, true);
    tab->config = to_bytes(config);

    const uie::splitter_item_full_v3_t* v3 = nullptr;
    const uie::splitter_item_full_t* full = nullptr;
    if (item->query(v3) && v3->get_extra_data_format_id() == guids::tab_extra_format) {
        // One of ours (copy and paste, or a move between two Better Tabs containers).
        stream_writer_memblock extra;
        v3->get_extra_data(&extra);
        tab->extra = decode_tab_extra(std::span<const std::uint8_t>(
            static_cast<const std::uint8_t*>(extra.m_data.get_ptr()), extra.m_data.get_size()));
    } else if (item->query(full)) {
        // Another splitter's item: keep its custom title. Its m_hidden means "collapsed" there,
        // not "no tab", so it is not carried over.
        tab->extra.use_custom_title = full->m_custom_title;
        pfc::string8 title;
        full->get_title(title);
        tab->extra.title = title.get_ptr();
    }
    return tab;
}

void TabsContainer::insert_panel(t_size index, const uie::splitter_item_t* item) {
    if (item == nullptr || index > tabs_.size()) return;
    auto tab = tab_from_item(item);
    Tab* raw = tab.get();
    tabs_.insert(tabs_.begin() + static_cast<std::ptrdiff_t>(index), std::move(tab));
    if (get_wnd() == nullptr) return;
    ensure_object(*raw);
    if (!settings_.lazy_children && tab_visible(*raw)) ensure_window(*raw);
    rebuild_strip();
    ensure_active_valid();
    layout();
    limits_changed();
}

void TabsContainer::remove_panel(t_size index) {
    if (index >= tabs_.size()) return;
    Tab* tab = tabs_[index].get();
    const bool was_active = tab == active_;
    if (get_wnd() != nullptr) {
        if (was_active) {
            // Move to the neighbour first, so the removal never exposes an empty area.
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
    tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(index));
    if (saved_active_ > index) --saved_active_;
    if (get_wnd() == nullptr) return;
    rebuild_strip();
    ensure_active_valid();
    layout();
    limits_changed();
}

void TabsContainer::replace_panel(t_size index, const uie::splitter_item_t* item) {
    if (item == nullptr || index >= tabs_.size()) return;
    auto tab = tab_from_item(item);
    Tab* raw = tab.get();
    const bool was_active = tabs_[index].get() == active_;
    if (get_wnd() != nullptr) destroy_tab_window(*tabs_[index]);
    if (was_active) active_ = nullptr;
    tabs_[index] = std::move(tab);
    if (get_wnd() == nullptr) return;
    ensure_object(*raw);
    if (!settings_.lazy_children && tab_visible(*raw)) ensure_window(*raw);
    rebuild_strip();
    if (was_active) activate(raw, false);
    ensure_active_valid();
    layout();
    limits_changed();
}

uie::splitter_item_t* TabsContainer::get_panel(t_size index) const {
    if (index >= tabs_.size()) return nullptr;
    const Tab& tab = *tabs_[index];
    auto* item = new uie::splitter_item_full_v3_impl_t;
    item->set_panel_guid(tab.guid);
    Bytes config = tab.config;
    if (tab.wnd != nullptr && tab.window.is_valid()) {
        try {
            pfc::array_t<t_uint8> live;
            tab.window->get_config_to_array(live, fb2k::noAbort, true);
            config = to_bytes(live);
        } catch (...) {
        }
    }
    item->set_panel_config_from_ptr(config.data(), config.size());
    item->set_window_ptr(tab.window);
    item->m_custom_title = tab.extra.use_custom_title;
    item->set_title(tab.extra.title.c_str(), tab.extra.title.size());
    item->m_hidden = tab.extra.hidden;
    item->m_autohide = false;
    item->m_caption_orientation = 0;
    item->m_locked = false;
    item->m_show_toggle_area = false;
    item->m_show_caption = false;
    item->m_size = 150;
    item->m_size_v2 = 150;
    item->m_size_v2_dpi = USER_DEFAULT_SCREEN_DPI;
    const Bytes extra = encode_tab_extra(tab.extra);
    item->m_extra_data.set_data_fromptr(extra.data(), extra.size());
    item->m_extra_data_format_id = guids::tab_extra_format;
    return item;
}

bool TabsContainer::is_point_ours(HWND wnd_point, const POINT& pt_screen,
                                  pfc::list_base_t<uie::window_ptr>& hierarchy) {
    // Same shape as Columns UI's Tab stack (splitter_tabs.h): the container itself and its strip
    // select the container; a point in a child selects the child with the container as its
    // parent, recursing into child splitters. Only created children can be under the point.
    const HWND self = get_wnd();
    if (self == nullptr || wnd_point == nullptr) return false;
    if (wnd_point != self && !IsChild(self, wnd_point)) return false;
    if (wnd_point == self || wnd_point == strip_.hwnd()) {
        hierarchy.add_item(this);
        return true;
    }
    for (const auto& tab : tabs_) {
        if (tab->wnd == nullptr || !tab->window.is_valid()) continue;
        uie::splitter_window_v2_ptr splitter;
        if (tab->window->service_query_t(splitter)) {
            pfc::list_t<uie::window_ptr> nested;
            nested.add_item(this);
            if (splitter->is_point_ours(wnd_point, pt_screen, nested)) {
                hierarchy.add_items(nested);
                return true;
            }
        } else if (wnd_point == tab->wnd || IsChild(tab->wnd, wnd_point)) {
            hierarchy.add_item(this);
            hierarchy.add_item(tab->window);
            return true;
        }
    }
    return false;
}

void TabsContainer::reorder_panels(const size_t* order, size_t count) {
    // new[i] = old[order[i]] - what Columns UI's own splitters do (the header's wording reads the
    // other way round). Anything that is not a permutation is ignored.
    if (order == nullptr || count != tabs_.size()) return;
    std::vector<bool> seen(count, false);
    for (size_t i = 0; i < count; ++i) {
        if (order[i] >= count || seen[order[i]]) return;
        seen[order[i]] = true;
    }
    std::vector<std::unique_ptr<Tab>> reordered(count);
    for (size_t i = 0; i < count; ++i) reordered[i] = std::move(tabs_[order[i]]);
    tabs_ = std::move(reordered);
    if (get_wnd() != nullptr) rebuild_strip();
}

bool TabsContainer::get_config_item_supported(t_size, const GUID& type) const {
    return type == uie::splitter_window::bool_use_custom_title ||
           type == uie::splitter_window::string_custom_title || type == uie::splitter_window::bool_hidden;
}

bool TabsContainer::get_config_item(t_size index, const GUID& type, stream_writer* out,
                                    abort_callback& abort) const {
    if (index >= tabs_.size()) return false;
    const Tab& tab = *tabs_[index];
    if (type == uie::splitter_window::bool_use_custom_title) {
        out->write_lendian_t(tab.extra.use_custom_title, abort);
        return true;
    }
    if (type == uie::splitter_window::string_custom_title) {
        out->write_string(tab.extra.title.c_str(), abort);
        return true;
    }
    if (type == uie::splitter_window::bool_hidden) {
        out->write_lendian_t(tab.extra.hidden, abort);
        return true;
    }
    return false;
}

bool TabsContainer::set_config_item(t_size index, const GUID& type, stream_reader* source,
                                    abort_callback& abort) {
    if (index >= tabs_.size()) return false;
    Tab& tab = *tabs_[index];
    if (type == uie::splitter_window::bool_use_custom_title) {
        source->read_lendian_t(tab.extra.use_custom_title, abort);
    } else if (type == uie::splitter_window::string_custom_title) {
        pfc::string8 title;
        source->read_string(title, abort);
        tab.extra.title = title.get_ptr();
    } else if (type == uie::splitter_window::bool_hidden) {
        source->read_lendian_t(tab.extra.hidden, abort);
    } else {
        return false;
    }
    update_label(tab);
    if (get_wnd() != nullptr) {
        rebuild_strip();
        ensure_active_valid();
        layout();
    }
    return true;
}

void TabsContainer::get_supported_panels(const pfc::list_base_const_t<uie::window::ptr>& windows,
                                         bit_array_var& mask_unsupported) {
    const uie::window_host_ptr host = host_for_availability();
    const t_size count = windows.get_count();
    for (t_size i = 0; i < count; ++i) mask_unsupported.set(i, !windows[i]->is_available(host));
}

// ---------------------------------------------------------------------------------------------
// Strip intents.

void TabsContainer::on_strip_activate(std::size_t index) noexcept {
    if (index < visible_.size()) activate(tabs_[visible_[index]].get(), true);
}

void TabsContainer::on_strip_step(int direction) noexcept {
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

void TabsContainer::show_tab_menu(std::size_t strip_index, POINT screen, bool with_panel_items,
                                  bool with_style) noexcept {
    const HWND self = get_wnd();
    if (self == nullptr) return;
    const service_ptr_t<TabsContainer> keep_alive(this);
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
        pfc::refcounted_object_ptr_t<uie::menu_hook_impl> hook = new uie::menu_hook_impl;
        if (with_panel_items && clicked != nullptr && clicked->wnd != nullptr && clicked->window.is_valid()) {
            clicked->window->get_menu_items(*hook.get_ptr());
            if (hook->get_children_count() > 0) {
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                hook->win32_build_menu(menu, menu_panel_base, 0x7FFF0000u - menu_panel_base);
            }
        }
        if (with_style) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            append_style_menu(menu);
        }
        const UINT cmd = static_cast<UINT>(TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD,
                                                          screen.x, screen.y, 0, self, nullptr));
        DestroyMenu(menu);
        menu = nullptr;
        if (cmd >= menu_panel_base) {
            hook->execute_by_id(cmd);
        } else if (cmd >= menu_style_base && cmd < style_last) {
            run_style_command(cmd);
        } else if (cmd >= menu_tab_base && cmd < menu_style_base) {
            const std::size_t s = cmd - menu_tab_base;
            // The tab list may have changed while the menu was open.
            if (s < snapshot.size() && snapshot == visible_) activate(tabs_[snapshot[s]].get(), true);
        }
    } catch (const std::exception& e) {
        log::warn(std::string("tab menu failed: ") + e.what());
    }
    if (menu != nullptr) DestroyMenu(menu);
}

void TabsContainer::on_strip_menu(std::size_t index, POINT screen) noexcept {
    show_tab_menu(index != no_index ? index : strip_index_of(active_), screen, index != no_index, true);
}

void TabsContainer::on_strip_overflow(POINT screen) noexcept { show_tab_menu(no_index, screen, false, false); }

void TabsContainer::append_style_menu(HMENU menu) const noexcept {
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
        radio(m, style_indicator_none, L"Text only", s.indicator == Indicator::none);
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING | (s.chip ? MF_CHECKED : 0), style_chip, L"Chips");
    }
    if (HMENU m = sub(L"Accent colour"); m != nullptr) {
        radio(m, style_accent_selection, L"Columns UI selection colour", s.accent_source == AccentSource::selection);
        radio(m, style_accent_cover, L"From the playing cover", s.accent_source == AccentSource::cover);
        radio(m, style_accent_custom, L"Custom...", s.accent_source == AccentSource::custom);
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
    }
    if (HMENU m = sub(L"Show strip"); m != nullptr) {
        radio(m, style_show_always, L"Always", s.visibility == StripVisibility::always);
        radio(m, style_show_two_or_more, L"Only with two or more tabs",
              s.visibility == StripVisibility::two_or_more);
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(style), L"Appearance");
}

void TabsContainer::run_style_command(unsigned command) noexcept {
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
    case style_chip: s.chip = !s.chip; break;
    case style_accent_selection: s.accent_source = AccentSource::selection; break;
    case style_accent_cover: s.accent_source = AccentSource::cover; break;
    case style_accent_custom: {
        static COLORREF custom_colours[16]{};
        CHOOSECOLORW cc{sizeof(cc)};
        cc.hwndOwner = core_api::get_main_window();
        cc.rgbResult = colour::colorref_from_rgb(s.accent_argb & 0xFFFFFFu);
        cc.lpCustColors = custom_colours;
        cc.Flags = CC_RGBINIT | CC_FULLOPEN;
        if (!ChooseColorW(&cc)) return;
        s.accent_argb = 0xFF000000u | colour::rgb_from_colorref(cc.rgbResult);
        s.accent_source = AccentSource::custom;
        break;
    }
    case style_sizing_fit: s.sizing = TabSizing::fit; break;
    case style_sizing_equal: s.sizing = TabSizing::equal; break;
    case style_sizing_fill: s.sizing = TabSizing::fill; break;
    case style_align_start: s.align = TabAlign::start; break;
    case style_align_centre: s.align = TabAlign::centre; break;
    case style_align_end: s.align = TabAlign::end; break;
    case style_show_always: s.visibility = StripVisibility::always; break;
    case style_show_two_or_more: s.visibility = StripVisibility::two_or_more; break;
    default: return;
    }
    apply_settings();
}

bool TabsContainer::on_strip_key(UINT message, WPARAM key) noexcept {
    try {
        if (message == WM_KEYDOWN && key == VK_TAB) {
            uie::window::g_on_tab(strip_.hwnd());
            return true;
        }
        const auto& parent = get_host();
        if (parent.is_valid() && !parent->get_keyboard_shortcuts_enabled()) return false;
        return uie::window::g_process_keydown_keyboard_shortcuts(key);
    } catch (...) {
        return false;
    }
}

void TabsContainer::on_strip_metrics_changed() noexcept {
    // A DPI change usually means another monitor: its text rendering parameters differ.
    refresh_font();
    layout();
    limits_changed();
}

uie::window_factory<TabsContainer> g_container_factory;

// ---------------------------------------------------------------------------------------------
// Colours and fonts pages. Singletons: fan changes out to the live containers.

void refresh_all_colours() noexcept {
    for (TabsContainer* container : live_containers()) container->refresh_colours();
}

void refresh_all_fonts() noexcept {
    for (TabsContainer* container : live_containers()) container->refresh_font();
}

class ColourClient : public cui::colours::client {
public:
    const GUID& get_client_guid() const override { return guids::colour_client; }
    void get_name(pfc::string_base& out) const override { out = BETTERTABS_NAME; }
    uint32_t get_supported_colours() const override {
        return cui::colours::colour_flag_background | cui::colours::colour_flag_text |
               cui::colours::colour_flag_selection_background;
    }
    uint32_t get_supported_bools() const override { return cui::colours::bool_flag_dark_mode_enabled; }
    bool get_themes_supported() const override { return false; }
    void on_colour_changed(uint32_t) const override { refresh_all_colours(); }
    void on_bool_changed(uint32_t mask) const override {
        if ((mask & cui::colours::bool_flag_dark_mode_enabled) != 0) refresh_all_colours();
    }
};

cui::colours::client::factory<ColourClient> g_colour_client;

class FontClient : public cui::fonts::client {
public:
    const GUID& get_client_guid() const override { return guids::font_client; }
    void get_name(pfc::string_base& out) const override { out = BETTERTABS_NAME ": tabs"; }
    cui::fonts::font_type_t get_default_font_type() const override { return cui::fonts::font_type_labels; }
    void on_font_changed() const override { refresh_all_fonts(); }
};

cui::fonts::client::factory<FontClient> g_font_client;

} // namespace

} // namespace bettertabs
