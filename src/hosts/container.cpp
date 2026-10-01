// The Columns UI container: a uie::splitter_window_v3 that shows one child at a time under a tab
// strip. Everything that does not depend on Columns UI lives in TabsCore (tabs_core.h); this
// file supplies the children (uie::window), the host handed to them, Columns UI's colours and
// fonts, and the splitter interface the Layout page and live editing use.
//
// Behaviour follows Columns UI's own Tab stack wherever the SDK leaves room for choice
// (foo_ui_columns/splitter_tabs.cpp): host semantics, missing panels, config items, export.

#include <helpers/foobar2000+atl.h>

#include <columns_ui-sdk/ui_extension.h>

#include <algorithm>
#include <memory>
#include <span>
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

[[nodiscard]] Bytes to_bytes(const pfc::array_t<t_uint8>& data) {
    return Bytes(data.get_ptr(), data.get_ptr() + data.get_size());
}

[[nodiscard]] Bytes read_all(stream_reader* reader, t_size size, abort_callback& abort) {
    Bytes bytes(size);
    if (size != 0) reader->read_object(bytes.data(), size, abort);
    return bytes;
}

struct CuiTab : Tab {
    //! The extension object. Exists as soon as the container does; its window only when shown.
    uie::window_ptr window;
    bool object_tried{false};
};

[[nodiscard]] CuiTab& cui(Tab& tab) noexcept { return static_cast<CuiTab&>(tab); }
[[nodiscard]] const CuiTab& cui(const Tab& tab) noexcept { return static_cast<const CuiTab&>(tab); }

class TabsHost;

class TabsContainer : public uie::container_uie_window_v3_t<uie::splitter_window_v3>, public TabsCore {
public:
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
    bool have_config_popup() const override { return true; }
    //! The Configure dialog. Also called by Columns UI's Layout page on an instance that has no
    //! window (it reads get_config afterwards).
    bool show_config_popup(HWND parent) override { return run_configure(parent); }

    uie::container_window_v3_config get_window_config() override {
        // Not transparent: that would repaint the whole container on every move and resize.
        return {L"foo_bettertabs_container", false};
    }
    LRESULT on_message(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) override {
        LRESULT result = 0;
        if (core_message(wnd, msg, wp, lp, result)) return result;
        return DefWindowProc(wnd, msg, wp, lp);
    }

    // uie::splitter_window ----------------------------------------------------------------

    void insert_panel(t_size index, const uie::splitter_item_t* item) override;
    void remove_panel(t_size index) override { remove_tab(index); }
    void replace_panel(t_size index, const uie::splitter_item_t* item) override;
    t_size get_panel_count() const override { return tabs_.size(); }
    bool get_config_item_supported(t_size index, const GUID& type) const override;
    bool get_config_item(t_size index, const GUID& type, stream_writer* out, abort_callback& abort) const override;
    bool set_config_item(t_size index, const GUID& type, stream_reader* source, abort_callback& abort) override;
    void get_supported_panels(const pfc::list_base_const_t<uie::window::ptr>& windows,
                              bit_array_var& mask_unsupported) override;
    void reorder_panels(const size_t* order, size_t count) override { reorder_tabs(order, count); }
    //! Layout editing (live edit, Ctrl+Shift+right-click) walks the tree through this. Without it the
    //! container cannot be selected and its children's menus lack the container entries.
    bool is_point_ours(HWND wnd_point, const POINT& pt_screen, pfc::list_base_t<uie::window_ptr>& hierarchy) override;

    // For the host -------------------------------------------------------------------------

    using TabsCore::find_by_wnd;
    using TabsCore::on_child_limits_changed;
    using TabsCore::show_child;
    void relinquish(HWND wnd) noexcept;
    void get_children(pfc::list_base_t<uie::window_ptr>& out) const;
    [[nodiscard]] bool self_visible() const;

    HWND core_wnd() const noexcept override { return get_wnd(); }

protected:
    uie::splitter_item_t* get_panel(t_size index) const override;

    // TabsCore hooks
    std::unique_ptr<Tab> host_new_tab() const override { return std::make_unique<CuiTab>(); }
    bool host_prepare(Tab& tab) noexcept override;
    bool host_present(const Tab& tab) const noexcept override { return cui(tab).window.is_valid(); }
    HWND host_create_window(Tab& tab) noexcept override;
    void host_destroy(Tab& tab) noexcept override;
    Bytes host_child_config(const Tab& tab) const override;
    std::wstring host_child_label(const Tab& tab) const override;
    std::wstring host_panel_name(const Tab& tab) const override;
    void host_limits_changed() noexcept override;
    bool host_visible() const noexcept override;
    HostColours host_colours() const noexcept override;
    void host_font(StripFont& font, StripTextOptions& options) const noexcept override;
    const wchar_t* host_ui_name() const noexcept override { return L"Columns UI"; }
    bool host_child_menu(Tab& tab, HMENU menu, unsigned first, unsigned last) noexcept override;
    void host_child_menu_command(unsigned id) noexcept override;
    void host_child_menu_done() noexcept override { menu_hook_.release(); }
    void host_tab_key(HWND from) noexcept override;
    bool host_shortcut(WPARAM key) noexcept override;
    service_ptr_t<service_base> host_keep_alive() noexcept override { return this; }
    void host_on_create() noexcept override;
    void host_on_destroy() noexcept override;

private:
    [[nodiscard]] std::unique_ptr<Tab> tab_from_item(const uie::splitter_item_t* item) const;
    [[nodiscard]] uie::window_host_ptr host_for_availability() const;

    service_ptr_t<TabsHost> host_;
    pfc::refcounted_object_ptr_t<uie::menu_hook_impl> menu_hook_;
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

void TabsContainer::set_config(stream_reader* reader, t_size size, abort_callback& abort) {
    InstanceData data;
    try {
        data = decode_instance(read_all(reader, size, abort));
    } catch (const std::exception& e) {
        // A damaged blob must not take the whole layout down: start empty with defaults.
        log::warn(std::string("could not read settings, using defaults: ") + e.what());
        data = InstanceData{};
    }
    reload(std::move(data));
}

void TabsContainer::get_config(stream_writer* writer, abort_callback& abort) const {
    const Bytes bytes = encode_instance(snapshot(true));
    writer->write(bytes.data(), bytes.size(), abort);
}

void TabsContainer::export_config(stream_writer* writer, abort_callback& abort) const {
    InstanceData data = snapshot(true);
    for (std::size_t i = 0; i < tabs_.size(); ++i) {
        uie::window_ptr child = cui(*tabs_[i]).window;
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
        if (objects[i].is_valid()) cui(*tabs_[i]).window = objects[i];
    }
}

// ---------------------------------------------------------------------------------------------
// Children.

uie::window_host_ptr TabsContainer::host_for_availability() const {
    if (host_.is_valid()) return host_;
    // Ownerless: a panel that kept this host from is_available() could never reach a dead container.
    return fb2k::service_new<TabsHost>();
}

bool TabsContainer::host_prepare(Tab& base) noexcept {
    CuiTab& tab = cui(base);
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

HWND TabsContainer::host_create_window(Tab& base) noexcept {
    CuiTab& tab = cui(base);
    if (!tab.window.is_valid()) return nullptr;
    try {
        const ui_helpers::window_position_t position(content_);
        return tab.window->create_or_transfer_window(get_wnd(), host_, position);
    } catch (const std::exception& e) {
        log::warn(std::string("could not create a panel window: ") + e.what());
        return nullptr;
    }
}

void TabsContainer::host_destroy(Tab& base) noexcept {
    CuiTab& tab = cui(base);
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
    tab.window.release();
    tab.object_tried = false;
}

Bytes TabsContainer::host_child_config(const Tab& base) const {
    const CuiTab& tab = cui(base);
    if (tab.wnd == nullptr || !tab.window.is_valid()) return tab.config;
    pfc::array_t<t_uint8> live;
    tab.window->get_config_to_array(live, fb2k::noAbort, true);
    return to_bytes(live);
}

std::wstring TabsContainer::host_child_label(const Tab& base) const {
    const CuiTab& tab = cui(base);
    pfc::string8 name;
    if (tab.window.is_valid()) {
        if (!tab.window->get_short_name(name)) tab.window->get_name(name);
    }
    return widen(name.get_ptr());
}

std::wstring TabsContainer::host_panel_name(const Tab& tab) const {
    try {
        uie::window_ptr object = cui(tab).window;
        if (!object.is_valid() && !uie::window::create_by_guid(tab.guid, object)) return L"(missing panel)";
        pfc::string8 name;
        object->get_name(name);
        return widen(name.get_ptr());
    } catch (...) {
        return L"(missing panel)";
    }
}

void TabsContainer::host_limits_changed() noexcept {
    const HWND self = get_wnd();
    const auto& parent = get_host();
    if (self == nullptr || !parent.is_valid()) return;
    try {
        parent->on_size_limit_change(self, uie::size_limit_all);
    } catch (...) {
    }
}

bool TabsContainer::host_visible() const noexcept {
    try {
        return self_visible();
    } catch (...) {
        return false;
    }
}

HostColours TabsContainer::host_colours() const noexcept {
    HostColours out;
    try {
        const cui::colours::helper colours(guids::colour_client);
        out.background = colours.get_colour(cui::colours::colour_background);
        out.text = colours.get_colour(cui::colours::colour_text);
        out.selection = colours.get_colour(cui::colours::colour_selection_background);
        out.dark = colours.is_dark_mode_active();
    } catch (...) {
    }
    return out;
}

void TabsContainer::host_font(StripFont& font, StripTextOptions& options) const noexcept {
    try {
        font.font = cui::fonts::get_log_font_with_fallback(guids::font_client);
        font.font_dpi = gfx::system_dpi();
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
    } catch (...) {
    }
}

bool TabsContainer::host_child_menu(Tab& base, HMENU menu, unsigned first, unsigned last) noexcept {
    CuiTab& tab = cui(base);
    if (!tab.window.is_valid()) return false;
    try {
        menu_hook_ = new uie::menu_hook_impl;
        tab.window->get_menu_items(*menu_hook_.get_ptr());
        if (menu_hook_->get_children_count() == 0) return false;
        menu_hook_->win32_build_menu(menu, first, last - first);
        return true;
    } catch (...) {
        return false;
    }
}

void TabsContainer::host_child_menu_command(unsigned id) noexcept {
    try {
        if (menu_hook_.is_valid()) menu_hook_->execute_by_id(id);
    } catch (...) {
    }
}

void TabsContainer::host_tab_key(HWND from) noexcept {
    try {
        uie::window::g_on_tab(from);
    } catch (...) {
    }
}

bool TabsContainer::host_shortcut(WPARAM key) noexcept {
    try {
        const auto& parent = get_host();
        if (parent.is_valid() && !parent->get_keyboard_shortcuts_enabled()) return false;
        return uie::window::g_process_keydown_keyboard_shortcuts(key);
    } catch (...) {
        return false;
    }
}

void TabsContainer::host_on_create() noexcept {
    try {
        host_ = fb2k::service_new<TabsHost>(this);
    } catch (...) {
    }
}

void TabsContainer::host_on_destroy() noexcept {
    if (host_.is_valid()) host_->detach();
    host_.release();
}

void TabsContainer::get_children(pfc::list_base_t<uie::window_ptr>& out) const {
    for (const auto& tab : tabs_) {
        if (cui(*tab).window.is_valid()) out.add_item(cui(*tab).window);
    }
}

bool TabsContainer::self_visible() const {
    const HWND self = get_wnd();
    if (self == nullptr) return false;
    const auto& parent = get_host();
    return parent.is_valid() ? parent->is_visible(self) : IsWindowVisible(self) != FALSE;
}

void TabsContainer::relinquish(HWND wnd) noexcept {
    // The child moved to another host: forget it without destroying it.
    Tab* tab = find_by_wnd(wnd);
    if (tab == nullptr) return;
    const std::size_t index = index_of(tab);
    const bool was_active = tab == active_;
    if (was_active) active_ = nullptr;
    tab->wnd = nullptr;
    cui(*tab).window.release();
    std::erase(config_tabs_, tab);
    tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(index));
    rebuild_strip();
    if (was_active) activate(fallback_after_removal(index), false);
    layout();
    limits_changed();
}

// ---------------------------------------------------------------------------------------------
// splitter_window.

std::unique_ptr<Tab> TabsContainer::tab_from_item(const uie::splitter_item_t* item) const {
    auto tab = std::make_unique<CuiTab>();
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
    insert_tab(index, tab_from_item(item), false);
}

void TabsContainer::replace_panel(t_size index, const uie::splitter_item_t* item) {
    if (item == nullptr || index >= tabs_.size()) return;
    replace_tab(index, tab_from_item(item));
}

uie::splitter_item_t* TabsContainer::get_panel(t_size index) const {
    if (index >= tabs_.size()) return nullptr;
    const CuiTab& tab = cui(*tabs_[index]);
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
        if (tab->wnd == nullptr || !cui(*tab).window.is_valid()) continue;
        uie::splitter_window_v2_ptr splitter;
        if (cui(*tab).window->service_query_t(splitter)) {
            pfc::list_t<uie::window_ptr> nested;
            nested.add_item(this);
            if (splitter->is_point_ours(wnd_point, pt_screen, nested)) {
                hierarchy.add_items(nested);
                return true;
            }
        } else if (wnd_point == tab->wnd || IsChild(tab->wnd, wnd_point)) {
            hierarchy.add_item(this);
            hierarchy.add_item(cui(*tab).window);
            return true;
        }
    }
    return false;
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

uie::window_factory<TabsContainer> g_container_factory;

// ---------------------------------------------------------------------------------------------
// Colours and fonts pages. Singletons: fan changes out to the live containers (the Default UI
// ones too: they ignore Columns UI's settings but refreshing them is harmless).

void refresh_all_colours() noexcept {
    for (TabsCore* container : TabsCore::live()) container->refresh_colours();
}

void refresh_all_fonts() noexcept {
    for (TabsCore* container : TabsCore::live()) container->refresh_font();
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
