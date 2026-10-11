// The Default UI container: a ui_element that shows one child element at a time under the same
// tab strip as the Columns UI container. Everything that does not depend on the UI lives in
// TabsCore (tabs_core.h); this file supplies the children (ui_element_instance), Default UI's
// colours and fonts, focus routing and layout editing.
//
// Layout editing follows foo_ui_std's own containers (ui_element_helpers): in edit mode a child's
// right click reaches us and gets the standard element menu plus our tab items; a right click on
// the strip goes to our parent, which shows the standard menu for this element with our items
// for the clicked tab (edit_mode_context_menu_*). An empty tab holds the "dummy" element, which
// offers to add an element when clicked.

#include <helpers/foobar2000+atl.h>

#include <helpers/ui_element_helpers.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "../guids.h"
#include "../model/codec.h"
#include "../platform/graphics.h"
#include "../platform/logging.h"
#include "../version.h"
#include "host_util.h"
#include "tabs_core.h"

namespace bettertabs {

namespace {

constexpr const wchar_t* window_class = L"foo_bettertabs_dui";
//! The id the "Replace UI Element" dialog gets for "add a new tab" (children use serials from 1).
constexpr unsigned add_new_id = 0xFFFFFFF0u;

enum EditCommand : unsigned {
    edit_add,
    edit_paste,
    edit_replace,
    edit_copy,
    edit_remove,
    edit_rename,
    edit_configure,
    edit_count,
};

enum ChildCommand : unsigned {
    child_remove,
    child_rename,
    child_configure,
    child_count,
};

[[nodiscard]] Bytes config_bytes(const ui_element_config::ptr& cfg) {
    if (!cfg.is_valid()) return {};
    return to_bytes(cfg->get_data(), cfg->get_data_size());
}

[[nodiscard]] ChildRecord empty_child() {
    ChildRecord child;
    child.guid = pfc::guid_null;
    child.extra = encode_tab_extra(TabExtra{});
    return child;
}

[[nodiscard]] InstanceData decode_or_default(const ui_element_config::ptr& cfg) {
    InstanceData data;
    try {
        if (cfg.is_valid() && cfg->get_guid() == guids::dui_element && cfg->get_data_size() != 0) {
            data = decode_instance(config_bytes(cfg));
        } else {
            data.children.push_back(empty_child());
        }
    } catch (const std::exception& e) {
        // A damaged blob must not take the whole layout down: start with one empty tab.
        log::warn(std::string("could not read settings, using defaults: ") + e.what());
        data = InstanceData{};
        data.children.push_back(empty_child());
    }
    return data;
}

[[nodiscard]] ui_element_config::ptr make_config(const InstanceData& data) {
    const Bytes bytes = encode_instance(data);
    return ui_element_config::g_create(guids::dui_element, bytes.data(), bytes.size());
}

class DuiContainer;

//! What a child element gets as its host. Forwards to the container until orphaned.
class ChildCallback : public ui_element_instance_callback_v3 {
public:
    ChildCallback(DuiContainer* owner, unsigned serial) noexcept : owner_(owner), serial_(serial) {}
    void orphan() noexcept { owner_ = nullptr; }

    void on_min_max_info_change() override;
    void on_alt_pressed(bool) override {}
    bool query_color(const GUID& what, t_ui_color& out) override;
    bool request_activation(service_ptr_t<ui_element_instance> item) override;
    bool is_edit_mode_enabled() override;
    void request_replace(service_ptr_t<ui_element_instance> item) override;
    t_ui_font query_font_ex(const GUID& what) override;
    bool is_elem_visible(service_ptr_t<ui_element_instance> elem) override;
    t_size notify(ui_element_instance* source, const GUID& what, t_size param1, const void* param2,
                  t_size param2size) override;

private:
    DuiContainer* owner_;
    const unsigned serial_;
};

struct DuiTab : Tab {
    ui_element_instance_ptr instance;
    service_ptr_t<ChildCallback> callback;
    //! Identifies the tab to its callback and to the edit menus; never reused.
    unsigned serial{0};
    bool prepared{false};
    GUID subclass{};
    //! The element's name, and the label it set for itself (set_elem_label).
    std::wstring name;
    std::wstring elem_label;
};

[[nodiscard]] DuiTab& dui(Tab& tab) noexcept { return static_cast<DuiTab&>(tab); }
[[nodiscard]] const DuiTab& dui(const Tab& tab) noexcept { return static_cast<const DuiTab&>(tab); }

class DuiContainer : public ui_element_instance,
                     public TabsCore,
                     private ui_element_helpers::ui_element_edit_tools {
public:
    explicit DuiContainer(ui_element_instance_callback::ptr callback) : callback_(std::move(callback)) {}
    ~DuiContainer() {
        // The reference count is already zero here: nothing may take a reference on us any more
        // (host_keep_alive, is_elem_visible_), or the object would be deleted twice.
        dying_ = true;
        if (wnd_ != nullptr) DestroyWindow(wnd_);
    }

    void initialize(HWND parent, const ui_element_config::ptr& cfg);

    // ui_element_instance ----------------------------------------------------------------------

    fb2k::hwnd_t get_wnd() override { return wnd_; }
    void set_configuration(ui_element_config::ptr cfg) override { reload(decode_or_default(cfg)); }
    ui_element_config::ptr get_configuration() override { return make_config(snapshot(true)); }
    GUID get_guid() override { return guids::dui_element; }
    GUID get_subclass() override { return ui_element_subclass_containers; }
    double get_focus_priority() override;
    void set_default_focus() override;
    bool get_focus_priority_subclass(double& out, const GUID& subclass) override;
    bool set_default_focus_subclass(const GUID& subclass) override;
    ui_element_min_max_info get_min_max_info() override;
    void notify(const GUID& what, t_size param1, const void* param2, t_size param2size) override;
    bool edit_mode_context_menu_test(const POINT&, bool) override { return true; }
    void edit_mode_context_menu_build(const POINT& point, bool from_keyboard, HMENU menu, unsigned base) override;
    void edit_mode_context_menu_command(const POINT& point, bool from_keyboard, unsigned id, unsigned base) override;

    HWND core_wnd() const noexcept override { return wnd_; }

    // From the children's callbacks ------------------------------------------------------------

    void child_limits_changed(unsigned serial) noexcept;
    bool child_query_color(const GUID& what, t_ui_color& out) { return callback_->query_color(what, out); }
    t_ui_font child_query_font(const GUID& what) { return callback_->query_font_ex(what); }
    bool edit_mode() const noexcept {
        try {
            return callback_->is_edit_mode_enabled();
        } catch (...) {
            return false;
        }
    }
    bool child_request_activation(unsigned serial);
    void child_request_replace(unsigned serial);
    bool child_visible(unsigned serial);
    t_size child_notify(unsigned serial, ui_element_instance* source, const GUID& what, t_size param1,
                        const void* param2, t_size param2size);

protected:
    // TabsCore hooks
    std::unique_ptr<Tab> host_new_tab() const override {
        auto tab = std::make_unique<DuiTab>();
        tab->serial = next_serial_++;
        return tab;
    }
    bool host_prepare(Tab& tab) noexcept override;
    bool host_present(const Tab&) const noexcept override { return true; }
    HWND host_create_window(Tab& tab) noexcept override;
    void host_destroy(Tab& tab) noexcept override;
    Bytes host_child_config(const Tab& tab) const override;
    std::wstring host_child_label(const Tab& tab) const override {
        const DuiTab& t = dui(tab);
        return t.elem_label.empty() ? t.name : t.elem_label;
    }
    std::wstring host_panel_name(const Tab& tab) const override;
    void host_query_limits(Tab& tab) noexcept override;
    void host_limits_changed() noexcept override {
        try {
            callback_->on_min_max_info_change();
        } catch (...) {
        }
    }
    bool host_visible() const noexcept override;
    HostColours host_colours() const noexcept override;
    void host_font(StripFont& font, StripTextOptions& options) const noexcept override;
    const wchar_t* host_ui_name() const noexcept override { return L"Default UI"; }
    const wchar_t* host_highlight_name() const noexcept override { return L"highlight colour"; }
    void host_child_shown(Tab& tab, bool shown) noexcept override;
    bool host_strip_menu(std::size_t index, POINT screen) noexcept override;
    void host_tab_key(HWND from) noexcept override;
    bool host_shortcut(WPARAM key) noexcept override;
    service_ptr_t<service_base> host_keep_alive() noexcept override {
        return dying_ ? service_ptr_t<service_base>() : service_ptr_t<service_base>(this);
    }

    // ui_element_edit_tools
    void host_replace_element(unsigned id, ui_element_config::ptr cfg) override;
    void host_replace_element(unsigned id, const GUID& guid) override;
    bool host_edit_mode_context_menu_test(unsigned, const POINT&, bool) override { return true; }
    void host_edit_mode_context_menu_build(unsigned id, const POINT& point, bool from_keyboard, HMENU menu,
                                           unsigned& id_base) override;
    void host_edit_mode_context_menu_command(unsigned id, const POINT& point, bool from_keyboard, unsigned cmd,
                                             unsigned id_base) override;

private:
    static LRESULT CALLBACK wnd_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;
    LRESULT on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;
    bool on_context_menu(WPARAM wp, LPARAM lp) noexcept;

    [[nodiscard]] DuiTab* by_serial(unsigned serial) const noexcept;
    [[nodiscard]] DuiTab* tab_containing(HWND wnd) const noexcept;
    [[nodiscard]] ui_element_config::ptr child_config(const DuiTab& tab) const;
    [[nodiscard]] std::unique_ptr<DuiTab> tab_from_config(const ui_element_config::ptr& cfg) const;
    //! A tab's best focus candidate for `subclass`; created children answer, others by kind.
    [[nodiscard]] DuiTab* focus_candidate(const GUID& subclass, double& priority) const;
    void remove_or_empty(DuiTab* tab) noexcept;
    void forward_notify(const GUID& what, t_size param1, const void* param2, t_size param2size, bool active_only);

    const ui_element_instance_callback::ptr callback_;
    HWND wnd_{nullptr};
    mutable unsigned next_serial_{1};
    bool dying_{false};
    //! The strip tab a right click in layout editing mode was on (strip index), while its menu is up.
    std::size_t edit_menu_index_{no_index};
    bool edit_menu_from_strip_{false};
    //! The tab the open edit menu is about.
    DuiTab* edit_menu_tab_{nullptr};
};

// ---------------------------------------------------------------------------------------------
// The child callback.

void ChildCallback::on_min_max_info_change() {
    if (owner_ != nullptr) owner_->child_limits_changed(serial_);
}

bool ChildCallback::query_color(const GUID& what, t_ui_color& out) {
    return owner_ != nullptr && owner_->child_query_color(what, out);
}

bool ChildCallback::request_activation(service_ptr_t<ui_element_instance>) {
    return owner_ != nullptr && owner_->child_request_activation(serial_);
}

bool ChildCallback::is_edit_mode_enabled() { return owner_ != nullptr && owner_->edit_mode(); }

void ChildCallback::request_replace(service_ptr_t<ui_element_instance>) {
    if (owner_ != nullptr) owner_->child_request_replace(serial_);
}

t_ui_font ChildCallback::query_font_ex(const GUID& what) {
    return owner_ != nullptr ? owner_->child_query_font(what) : nullptr;
}

bool ChildCallback::is_elem_visible(service_ptr_t<ui_element_instance>) {
    return owner_ != nullptr && owner_->child_visible(serial_);
}

t_size ChildCallback::notify(ui_element_instance* source, const GUID& what, t_size param1, const void* param2,
                             t_size param2size) {
    return owner_ != nullptr ? owner_->child_notify(serial_, source, what, param1, param2, param2size) : 0;
}

// ---------------------------------------------------------------------------------------------
// Window.

void DuiContainer::initialize(HWND parent, const ui_element_config::ptr& cfg) {
    load(decode_or_default(cfg));
    static ATOM atom = 0;
    if (atom == 0) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = &DuiContainer::wnd_proc;
        wc.hInstance = core_api::get_my_instance();
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = window_class;
        atom = RegisterClassExW(&wc);
        if (atom == 0) throw std::runtime_error("could not register the container window class");
    }
    const HWND wnd = CreateWindowExW(WS_EX_CONTROLPARENT, window_class, L"",
                                     WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0, 0, parent, nullptr,
                                     core_api::get_my_instance(), this);
    if (wnd == nullptr) throw std::runtime_error("could not create the container window");
}

LRESULT CALLBACK DuiContainer::wnd_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    DuiContainer* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = static_cast<DuiContainer*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self != nullptr) self->wnd_ = wnd;
    } else {
        self = reinterpret_cast<DuiContainer*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    }
    if (self == nullptr) return DefWindowProcW(wnd, msg, wp, lp);
    return self->on_message(wnd, msg, wp, lp);
}

LRESULT DuiContainer::on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    try {
        switch (msg) {
        case WM_NCDESTROY:
            SetWindowLongPtrW(wnd, GWLP_USERDATA, 0);
            wnd_ = nullptr;
            return DefWindowProcW(wnd, msg, wp, lp);
        case WM_DESTROY: {
            // The edit tools close a "Replace UI Element" dialog that is still open.
            LRESULT ignored = 0;
            ProcessWindowMessage(wnd, msg, wp, lp, ignored);
            break;
        }
        case WM_CONTEXTMENU:
            if (on_context_menu(wp, lp)) return 0;
            // Our own window or the strip: the parent shows the menu for this element.
            return DefWindowProcW(wnd, msg, wp, lp);
        default: break;
        }
        LRESULT result = 0;
        if (core_message(wnd, msg, wp, lp, result)) return result;
    } catch (const std::exception& e) {
        log::warn(std::string("container message failed: ") + e.what());
    } catch (...) {
        log::warn("container message failed");
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

bool DuiContainer::on_context_menu(WPARAM wp, LPARAM lp) noexcept {
    // Children forward their right click here in layout editing mode (DefWindowProc).
    if (!edit_mode()) return false;
    DuiTab* tab = tab_containing(reinterpret_cast<HWND>(wp));
    if (tab == nullptr || !tab->instance.is_valid()) return false;
    const service_ptr_t<service_base> keep_alive = host_keep_alive();
    const ui_element_instance_ptr item = tab->instance;
    try {
        standard_edit_context_menu(lp, item, tab->serial, wnd_);
    } catch (const std::exception& e) {
        log::warn(std::string("layout menu failed: ") + e.what());
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Children.

DuiTab* DuiContainer::by_serial(unsigned serial) const noexcept {
    for (const auto& tab : tabs_) {
        if (dui(*tab).serial == serial) return &dui(*tab);
    }
    return nullptr;
}

DuiTab* DuiContainer::tab_containing(HWND wnd) const noexcept {
    if (wnd == nullptr) return nullptr;
    for (const auto& tab : tabs_) {
        if (tab->wnd != nullptr && (tab->wnd == wnd || IsChild(tab->wnd, wnd))) return &dui(*tab);
    }
    return nullptr;
}

ui_element_config::ptr DuiContainer::child_config(const DuiTab& tab) const {
    const Bytes bytes = host_child_config(tab);
    return ui_element_config::g_create(tab.guid, bytes.data(), bytes.size());
}

std::unique_ptr<DuiTab> DuiContainer::tab_from_config(const ui_element_config::ptr& cfg) const {
    auto tab = std::make_unique<DuiTab>();
    tab->serial = next_serial_++;
    tab->guid = cfg.is_valid() ? cfg->get_guid() : pfc::guid_null;
    tab->config = config_bytes(cfg);
    return tab;
}

bool DuiContainer::host_prepare(Tab& base) noexcept {
    DuiTab& tab = dui(base);
    if (tab.prepared) return true;
    tab.prepared = true;
    try {
        if (tab.guid == pfc::guid_null) {
            tab.name = L"(empty)";
        } else if (service_ptr_t<ui_element> elem; ui_element_helpers::find(elem, tab.guid)) {
            pfc::string8 name;
            elem->get_name(name);
            tab.name = widen(name.get_ptr());
            tab.subclass = elem->get_subclass();
        } else {
            tab.name = L"(missing element)";
        }
    } catch (...) {
        tab.name = L"(missing element)";
    }
    update_label(tab);
    return true;
}

HWND DuiContainer::host_create_window(Tab& base) noexcept {
    DuiTab& tab = dui(base);
    if (wnd_ == nullptr) return nullptr;
    try {
        tab.callback = new service_impl_t<ChildCallback>(this, tab.serial);
        const ui_element_config::ptr cfg = ui_element_config::g_create(tab.guid, tab.config.data(), tab.config.size());
        // Falls back to the "dummy" element (and says so in the console) if the element is missing.
        tab.instance = ui_element_helpers::instantiate(wnd_, cfg, tab.callback);
        if (!tab.instance.is_valid()) return nullptr;
        const HWND wnd = tab.instance->get_wnd();
        if (wnd == nullptr) return nullptr;
        SetWindowPos(wnd, nullptr, content_.left, content_.top, content_.right - content_.left,
                     content_.bottom - content_.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_HIDEWINDOW);
        return wnd;
    } catch (const std::exception& e) {
        log::warn(std::string("could not create an element: ") + e.what());
        if (tab.callback.is_valid()) tab.callback->orphan();
        tab.callback.release();
        tab.instance.release();
        return nullptr;
    }
}

void DuiContainer::host_destroy(Tab& base) noexcept {
    DuiTab& tab = dui(base);
    if (tab.instance.is_valid()) {
        try {
            const ui_element_config::ptr cfg = tab.instance->get_configuration();
            // A missing element's stand-in has no configuration of its own: keep the original.
            if (cfg.is_valid() && cfg->get_guid() == tab.guid) tab.config = config_bytes(cfg);
        } catch (...) {
        }
        try {
            const HWND wnd = tab.instance->get_wnd();
            if (wnd != nullptr && IsWindow(wnd)) DestroyWindow(wnd);
        } catch (...) {
        }
        tab.instance.release();
    }
    if (tab.callback.is_valid()) tab.callback->orphan();
    tab.callback.release();
}

Bytes DuiContainer::host_child_config(const Tab& base) const {
    const DuiTab& tab = dui(base);
    if (!tab.instance.is_valid()) return tab.config;
    const ui_element_config::ptr cfg = tab.instance->get_configuration();
    if (!cfg.is_valid() || cfg->get_guid() != tab.guid) return tab.config;
    return config_bytes(cfg);
}

std::wstring DuiContainer::host_panel_name(const Tab& base) const {
    const DuiTab& tab = dui(base);
    if (!tab.name.empty()) return tab.name;
    try {
        if (tab.guid == pfc::guid_null) return L"(empty)";
        service_ptr_t<ui_element> elem;
        if (!ui_element_helpers::find(elem, tab.guid)) return L"(missing element)";
        pfc::string8 name;
        elem->get_name(name);
        return widen(name.get_ptr());
    } catch (...) {
        return L"(missing element)";
    }
}

void DuiContainer::host_query_limits(Tab& base) noexcept {
    DuiTab& tab = dui(base);
    if (!tab.instance.is_valid()) {
        TabsCore::host_query_limits(base);
        return;
    }
    try {
        const ui_element_min_max_info info = tab.instance->get_min_max_info();
        const auto cap = [](t_uint32 v) {
            return static_cast<unsigned>((std::min)(v, static_cast<t_uint32>(limit_cap)));
        };
        tab.limits.min_width = cap(info.m_min_width);
        tab.limits.min_height = cap(info.m_min_height);
        tab.limits.max_width = cap(info.m_max_width);
        tab.limits.max_height = cap(info.m_max_height);
    } catch (...) {
        tab.limits = Limits{};
    }
}

void DuiContainer::host_child_shown(Tab& base, bool shown) noexcept {
    DuiTab& tab = dui(base);
    if (dying_ || !tab.instance.is_valid()) return;
    try {
        if (callback_->is_elem_visible_(this)) {
            tab.instance->notify(ui_element_notify_visibility_changed, shown ? 1 : 0, nullptr, 0);
        }
    } catch (...) {
    }
}

void DuiContainer::child_limits_changed(unsigned serial) noexcept {
    if (const DuiTab* tab = by_serial(serial); tab != nullptr && tab->wnd != nullptr) {
        on_child_limits_changed(tab->wnd);
    }
}

bool DuiContainer::child_request_activation(unsigned serial) {
    DuiTab* tab = by_serial(serial);
    if (dying_ || tab == nullptr || !tab_visible(*tab)) return false;
    // Our own parent first: we may be a hidden tab of an outer container.
    if (!callback_->request_activation(this)) return false;
    activate(tab, false);
    return active_ == tab;
}

void DuiContainer::child_request_replace(unsigned serial) {
    DuiTab* tab = by_serial(serial);
    if (tab == nullptr || wnd_ == nullptr) return;
    const HWND anchor = tab->instance.is_valid() ? tab->instance->get_wnd() : wnd_;
    replace_dialog(anchor != nullptr ? anchor : wnd_, serial, tab->guid);
}

bool DuiContainer::child_visible(unsigned serial) {
    const DuiTab* tab = by_serial(serial);
    return tab != nullptr && tab == active_ && host_visible();
}

t_size DuiContainer::child_notify(unsigned serial, ui_element_instance* source, const GUID& what, t_size param1,
                                  const void* param2, t_size param2size) {
    if (what == ui_element_host_notify_set_elem_label) {
        DuiTab* tab = by_serial(serial);
        if (tab == nullptr) return 0;
        tab->elem_label = widen(static_cast<const char*>(param2));
        const std::wstring before = tab->label;
        update_label(*tab);
        if (tab->label != before && wnd_ != nullptr) rebuild_strip();
        return 1;
    }
    // Anything else (dialog texture, borders) is the host's business.
    return callback_->notify_(source, what, param1, param2, param2size);
}

// ---------------------------------------------------------------------------------------------
// Appearance and keys.

bool DuiContainer::host_visible() const noexcept {
    if (dying_ || wnd_ == nullptr || !IsWindowVisible(wnd_)) return false;
    try {
        return callback_->is_elem_visible_(const_cast<DuiContainer*>(this));
    } catch (...) {
        return true;
    }
}

HostColours DuiContainer::host_colours() const noexcept {
    HostColours out;
    try {
        out.background = callback_->query_std_color(ui_color_background);
        out.text = callback_->query_std_color(ui_color_text);
        out.selection = callback_->query_std_color(ui_color_selection);
        out.highlight = callback_->query_std_color(ui_color_highlight);
        out.dark = callback_->is_dark_mode();
    } catch (...) {
    }
    return out;
}

void DuiContainer::host_font(StripFont& font, StripTextOptions&) const noexcept {
    try {
        font.font_dpi = gfx::system_dpi();
        const auto handle = static_cast<HFONT>(callback_->query_font_ex(ui_font_tabs));
        if (handle != nullptr && GetObjectW(handle, sizeof(font.font), &font.font) == sizeof(font.font)) return;
        NONCLIENTMETRICSW metrics{sizeof(metrics)};
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
            font.font = metrics.lfMessageFont;
        }
    } catch (...) {
    }
}

bool DuiContainer::host_strip_menu(std::size_t index, POINT screen) noexcept {
    if (!edit_mode() || wnd_ == nullptr) return false;
    const HWND parent = GetParent(wnd_);
    if (parent == nullptr) return false;
    const service_ptr_t<service_base> keep_alive = host_keep_alive();
    edit_menu_index_ = index;
    edit_menu_from_strip_ = true;
    SendMessageW(parent, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(wnd_),
                 MAKELPARAM(static_cast<WORD>(screen.x), static_cast<WORD>(screen.y)));
    edit_menu_from_strip_ = false;
    edit_menu_index_ = no_index;
    return true;
}

void DuiContainer::host_tab_key(HWND from) noexcept {
    const HWND root = GetAncestor(wnd_, GA_ROOT);
    if (root == nullptr) return;
    const HWND next = GetNextDlgTabItem(root, from, GetKeyState(VK_SHIFT) < 0);
    if (next != nullptr && next != from) SetFocus(next);
}

bool DuiContainer::host_shortcut(WPARAM key) noexcept {
    try {
        return keyboard_shortcut_manager::get()->on_keydown_auto(key);
    } catch (...) {
        return false;
    }
}

// ---------------------------------------------------------------------------------------------
// ui_element_instance.

ui_element_min_max_info DuiContainer::get_min_max_info() {
    ui_element_min_max_info info;
    const auto open = [](unsigned v) {
        return v >= static_cast<unsigned>(limit_cap) ? UINT32_MAX : static_cast<t_uint32>(v);
    };
    info.m_min_width = limits_.min_width;
    info.m_min_height = limits_.min_height;
    info.m_max_width = open(limits_.max_width);
    info.m_max_height = open(limits_.max_height);
    return info;
}

DuiTab* DuiContainer::focus_candidate(const GUID& subclass, double& priority) const {
    DuiTab* best = nullptr;
    double best_priority = 0.0;
    for (const auto& base : tabs_) {
        DuiTab& tab = dui(*base);
        if (!tab_visible(tab)) continue;
        double p = 0.0;
        bool match = false;
        if (tab.instance.is_valid()) {
            match = tab.instance->get_focus_priority_subclass(p, subclass);
        } else if (tab.prepared && tab.subclass == subclass) {
            match = true; // not created yet (lazy): its kind is all we know
            p = 1.0;
        }
        if (!match) continue;
        // The shown tab wins ties.
        if (best == nullptr || p > best_priority || (p == best_priority && &tab == active_)) {
            best = &tab;
            best_priority = p;
        }
    }
    priority = best_priority;
    return best;
}

double DuiContainer::get_focus_priority() {
    const DuiTab* tab = active_ != nullptr ? &dui(*active_) : nullptr;
    return tab != nullptr && tab->instance.is_valid() ? tab->instance->get_focus_priority() : 0.0;
}

void DuiContainer::set_default_focus() {
    const DuiTab* tab = active_ != nullptr ? &dui(*active_) : nullptr;
    if (tab != nullptr && tab->instance.is_valid()) {
        tab->instance->set_default_focus();
    } else {
        set_default_focus_fallback();
    }
}

bool DuiContainer::get_focus_priority_subclass(double& out, const GUID& subclass) {
    double priority = 0.0;
    if (focus_candidate(subclass, priority) == nullptr) return false;
    out = priority;
    return true;
}

bool DuiContainer::set_default_focus_subclass(const GUID& subclass) {
    double priority = 0.0;
    DuiTab* tab = focus_candidate(subclass, priority);
    if (tab == nullptr) return false;
    activate(tab, false); // brings the tab up, creating its element if need be
    if (active_ != tab || !tab->instance.is_valid()) return false;
    return tab->instance->set_default_focus_subclass(subclass);
}

void DuiContainer::forward_notify(const GUID& what, t_size param1, const void* param2, t_size param2size,
                                  bool active_only) {
    // A copy: a child may change the tabs in response.
    std::vector<ui_element_instance_ptr> targets;
    for (const auto& base : tabs_) {
        const DuiTab& tab = dui(*base);
        if (!tab.instance.is_valid() || (active_only && &tab != active_)) continue;
        targets.push_back(tab.instance);
    }
    for (const auto& target : targets) target->notify(what, param1, param2, param2size);
}

void DuiContainer::notify(const GUID& what, t_size param1, const void* param2, t_size param2size) {
    if (what == ui_element_notify_colors_changed) {
        refresh_colours();
        forward_notify(what, param1, param2, param2size, false);
    } else if (what == ui_element_notify_font_changed) {
        refresh_font();
        forward_notify(what, param1, param2, param2size, false);
    } else if (what == ui_element_notify_visibility_changed) {
        // Only the shown tab changes visibility with us; the others stay hidden.
        forward_notify(what, param1, param2, param2size, true);
    } else if (what == ui_element_notify_get_element_labels) {
        // The layout editing overlay: the shown child only (as foo_ui_std's tabs).
        if (active_ == nullptr || param2 == nullptr) return;
        const DuiTab& tab = dui(*active_);
        if (!tab.instance.is_valid()) return;
        if (tab.instance->get_subclass() == ui_element_subclass_containers) {
            tab.instance->notify(what, param1, param2, param2size);
        } else if (tab.instance->get_guid() != pfc::guid_null && tab.instance->get_wnd() != nullptr) {
            auto* labels = reinterpret_cast<ui_element_notify_get_element_labels_callback*>(const_cast<void*>(param2));
            labels->set_visible_element(tab.instance);
        }
    } else {
        forward_notify(what, param1, param2, param2size, false);
    }
}

// ---------------------------------------------------------------------------------------------
// Layout editing.

void DuiContainer::edit_mode_context_menu_build(const POINT&, bool from_keyboard, HMENU menu, unsigned base) {
    edit_menu_tab_ = nullptr;
    if (edit_menu_from_strip_ && edit_menu_index_ < visible_.size()) {
        edit_menu_tab_ = &dui(*tabs_[visible_[edit_menu_index_]]);
    } else if (from_keyboard && active_ != nullptr) {
        edit_menu_tab_ = &dui(*active_);
    }
    bool can_paste = false;
    try {
        can_paste = ui_element_common_methods::get()->is_paste_available();
    } catch (...) {
    }
    // Greyed while the Configure dialog is open: its OK or Cancel would undo these.
    const UINT editing = configuring() ? MF_GRAYED : 0;
    AppendMenuW(menu, MF_STRING | editing, base + edit_add, L"Add new tab...");
    AppendMenuW(menu, MF_STRING | (can_paste ? editing : MF_GRAYED), base + edit_paste, L"Paste as new tab");
    if (edit_menu_tab_ != nullptr) {
        const std::wstring name = menu_text(edit_menu_tab_->label.empty() ? edit_menu_tab_->name : edit_menu_tab_->label);
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, (L"Tab: " + name).c_str());
        AppendMenuW(menu, MF_STRING | editing, base + edit_replace, L"Replace tab's element...");
        AppendMenuW(menu, MF_STRING | (edit_menu_tab_->guid == pfc::guid_null ? MF_GRAYED : 0), base + edit_copy,
                    L"Copy tab's element");
        AppendMenuW(menu, MF_STRING | editing, base + edit_rename, L"Rename tab...");
        AppendMenuW(menu, MF_STRING | editing, base + edit_remove, L"Remove tab");
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, base + edit_configure, L"Configure " BETTERTABS_NAME L"...");
}

void DuiContainer::edit_mode_context_menu_command(const POINT&, bool, unsigned id, unsigned base) {
    const service_ptr_t<service_base> keep_alive = host_keep_alive();
    DuiTab* tab = edit_menu_tab_;
    edit_menu_tab_ = nullptr;
    if (tab != nullptr && index_of(tab) == no_index) tab = nullptr; // gone meanwhile
    if (id < base || wnd_ == nullptr) return;
    switch (id - base) {
    case edit_add: replace_dialog(wnd_, add_new_id, pfc::guid_null); break;
    case edit_paste: host_paste_element(add_new_id); break;
    case edit_replace:
        if (tab != nullptr) child_request_replace(tab->serial);
        break;
    case edit_copy:
        if (tab != nullptr) ui_element_common_methods::get()->copy(child_config(*tab));
        break;
    case edit_rename:
        if (tab != nullptr) rename_tab(tab);
        break;
    case edit_remove:
        if (tab != nullptr) remove_or_empty(tab);
        break;
    case edit_configure: run_configure(wnd_, true); break;
    default: break;
    }
}

void DuiContainer::host_edit_mode_context_menu_build(unsigned id, const POINT&, bool, HMENU menu,
                                                     unsigned& id_base) {
    const DuiTab* tab = by_serial(id);
    const UINT editing = tab == nullptr || configuring() ? MF_GRAYED : 0;
    AppendMenuW(menu, MF_STRING | editing, id_base + child_rename, L"Rename tab...");
    AppendMenuW(menu, MF_STRING | editing, id_base + child_remove, L"Remove tab");
    AppendMenuW(menu, MF_STRING, id_base + child_configure, L"Configure " BETTERTABS_NAME L"...");
    id_base += child_count;
}

void DuiContainer::host_edit_mode_context_menu_command(unsigned id, const POINT&, bool, unsigned cmd,
                                                       unsigned id_base) {
    if (cmd < id_base) return;
    const service_ptr_t<service_base> keep_alive = host_keep_alive();
    DuiTab* tab = by_serial(id);
    switch (cmd - id_base) {
    case child_rename:
        if (tab != nullptr) rename_tab(tab);
        break;
    case child_remove:
        if (tab != nullptr) remove_or_empty(tab);
        break;
    case child_configure: run_configure(wnd_, true); break;
    default: break;
    }
}

void DuiContainer::remove_or_empty(DuiTab* tab) noexcept {
    const std::size_t index = index_of(tab);
    if (index == no_index) return;
    if (tabs_.size() > 1) {
        remove_tab(index);
        return;
    }
    // The last tab becomes an empty one, so there is always something to add an element to.
    try {
        auto empty = tab_from_config(ui_element_config::g_create_empty());
        replace_tab(index, std::move(empty));
    } catch (...) {
    }
}

void DuiContainer::host_replace_element(unsigned id, const GUID& guid) {
    try {
        ui_element_config::ptr cfg;
        if (guid == pfc::guid_null) {
            cfg = ui_element_config::g_create_empty();
        } else {
            service_ptr_t<ui_element> elem;
            if (!ui_element_helpers::find(elem, guid)) return;
            // Like foo_ui_std: the new element may take over the old one's settings (a splitter
            // its children, for one).
            if (const DuiTab* old = by_serial(id); old != nullptr && old->guid != pfc::guid_null) {
                cfg = elem->import(child_config(*old));
            }
            if (!cfg.is_valid()) cfg = elem->get_default_configuration();
        }
        host_replace_element(id, cfg);
    } catch (const std::exception& e) {
        log::warn(std::string("could not replace an element: ") + e.what());
    }
}

void DuiContainer::host_replace_element(unsigned id, ui_element_config::ptr cfg) {
    if (!cfg.is_valid()) return;
    try {
        auto tab = tab_from_config(cfg);
        DuiTab* raw = tab.get();
        if (id == add_new_id) {
            // After the shown tab, and shown.
            const std::size_t active = index_of(active_);
            insert_tab(active != no_index ? active + 1 : tabs_.size(), std::move(tab), true);
            return;
        }
        DuiTab* old = by_serial(id);
        if (old == nullptr) return;
        tab->extra = old->extra;
        replace_tab(index_of(old), std::move(tab));
        if (wnd_ != nullptr && index_of(raw) != no_index) activate(raw, true);
    } catch (const std::exception& e) {
        log::warn(std::string("could not replace an element: ") + e.what());
    }
}

// ---------------------------------------------------------------------------------------------
// The element: its children for the layout tools, and taking over another container's.

class Children : public ui_element_children_enumerator {
public:
    explicit Children(InstanceData data) : data_(std::move(data)) {}
    t_size get_count() override { return data_.children.size(); }
    ui_element_config::ptr get_item(t_size index) override {
        if (index >= data_.children.size()) return ui_element_config::g_create_empty();
        const ChildRecord& child = data_.children[index];
        return ui_element_config::g_create(child.guid, child.config.data(), child.config.size());
    }
    bool can_set_count() override { return true; }
    void set_count(t_size count) override {
        if (count < data_.children.size()) {
            data_.children.resize(count);
        } else {
            while (data_.children.size() < count) data_.children.push_back(empty_child());
        }
        if (data_.active >= data_.children.size()) data_.active = 0;
    }
    void set_item(t_size index, ui_element_config::ptr cfg) override {
        if (index >= data_.children.size() || !cfg.is_valid()) return;
        data_.children[index].guid = cfg->get_guid();
        data_.children[index].config = config_bytes(cfg);
    }
    ui_element_config::ptr commit() override { return make_config(data_); }

private:
    InstanceData data_;
};

class DuiElement : public ui_element_v2 {
public:
    GUID get_guid() override { return guids::dui_element; }
    GUID get_subclass() override { return ui_element_subclass_containers; }
    void get_name(pfc::string_base& out) override { out = BETTERTABS_NAME; }
    bool get_description(pfc::string_base& out) override {
        out = "Shows one element at a time under a fast, modern tab strip.";
        return true;
    }
    t_uint32 get_flags() override { return 0; }
    bool bump() override { return false; }

    ui_element_instance_ptr instantiate(HWND parent, ui_element_config::ptr cfg,
                                        ui_element_instance_callback_ptr callback) override {
        PFC_ASSERT(cfg->get_guid() == get_guid());
        auto instance = fb2k::service_new<DuiContainer>(callback);
        instance->initialize(parent, cfg);
        return instance;
    }

    ui_element_config::ptr get_default_configuration() override {
        InstanceData data;
        data.children.push_back(empty_child());
        return make_config(data);
    }

    ui_element_children_enumerator_ptr enumerate_children(ui_element_config::ptr cfg) override {
        return new service_impl_t<Children>(decode_or_default(cfg));
    }

    ui_element_config::ptr import(ui_element_config::ptr cfg) override {
        if (!cfg.is_valid()) return nullptr;
        if (cfg->get_guid() == get_guid()) return cfg;
        // Another container (Tabs, a splitter): take over its children.
        const ui_element_children_enumerator_ptr children = ui_element_helpers::enumerate_children(cfg);
        if (!children.is_valid()) return nullptr;
        InstanceData data;
        const t_size count = children->get_count();
        for (t_size i = 0; i < count; ++i) {
            const ui_element_config::ptr item = children->get_item(i);
            ChildRecord child = empty_child();
            if (item.is_valid()) {
                child.guid = item->get_guid();
                child.config = config_bytes(item);
            }
            data.children.push_back(std::move(child));
        }
        if (data.children.empty()) data.children.push_back(empty_child());
        return make_config(data);
    }
};

FB2K_SERVICE_FACTORY(DuiElement);

} // namespace

} // namespace bettertabs
