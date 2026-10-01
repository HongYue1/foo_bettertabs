// The Configure dialog and the Rename dialog. Layout: foo_bettertabs.rc (style profile at the top);
// conventions: foobar2000-component-dev/references/preferences-pages.md (one child dialog per
// page with its own dark-mode hooks, guarded WM_NOTIFY, padded edits, swatch + hex colours).

#include <helpers/foobar2000+atl.h>

#include <helpers/DarkMode.h>
#include <helpers/atl-misc.h>

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

#include "../../resource.h"
#include "configure_dialog.h"

namespace bettertabs {

namespace {

constexpr int page_count = 5;

//! A control by id on the dialog itself or on one of its pages (ids are unique across pages).
[[nodiscard]] HWND find_control(HWND dialog, int control) {
    if (HWND direct = ::GetDlgItem(dialog, control)) return direct;
    for (HWND w = ::GetWindow(dialog, GW_CHILD); w != nullptr; w = ::GetWindow(w, GW_HWNDNEXT)) {
        wchar_t name[16]{};
        ::GetClassNameW(w, name, 16);
        if (wcscmp(name, L"#32770") != 0) continue;
        if (HWND found = ::GetDlgItem(w, control)) return found;
    }
    return nullptr;
}

[[nodiscard]] std::wstring window_text(HWND wnd) {
    if (wnd == nullptr) return {};
    const int length = ::GetWindowTextLengthW(wnd);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    const int got = ::GetWindowTextW(wnd, text.data(), length + 1);
    text.resize(static_cast<std::size_t>((std::max)(0, got)));
    return text;
}

[[nodiscard]] std::string to_utf8(const std::wstring& text) {
    return std::string(pfc::stringcvt::string_utf8_from_wide(text.c_str()).get_ptr());
}

[[nodiscard]] std::wstring to_wide(const std::string& text) {
    return std::wstring(pfc::stringcvt::string_wide_from_utf8(text.c_str(), text.size()).get_ptr());
}

//! Six hex digits. Junk reads as the fallback rather than as black, which would look like a bug.
[[nodiscard]] std::uint32_t parse_rgb(const std::wstring& text, std::uint32_t fallback) {
    std::wstring t = text;
    if (!t.empty() && t.front() == L'#') t.erase(0, 1);
    if (t.empty() || t.size() > 6) return fallback;
    std::uint32_t value = 0;
    for (const wchar_t c : t) {
        std::uint32_t digit = 0;
        if (c >= L'0' && c <= L'9') {
            digit = static_cast<std::uint32_t>(c - L'0');
        } else if (c >= L'a' && c <= L'f') {
            digit = static_cast<std::uint32_t>(c - L'a' + 10);
        } else if (c >= L'A' && c <= L'F') {
            digit = static_cast<std::uint32_t>(c - L'A' + 10);
        } else {
            return fallback;
        }
        value = (value << 4) | digit;
    }
    return 0xFF000000u | value;
}

[[nodiscard]] std::wstring format_rgb(std::uint32_t argb) {
    wchar_t buffer[8]{};
    std::swprintf(buffer, 8, L"%06X", argb & 0xFFFFFFu);
    return buffer;
}

[[nodiscard]] COLORREF colorref(std::uint32_t argb) {
    return RGB((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF);
}

//! Text fields get some room before the first character (4 px at 96 DPI).
void pad_text_fields(HWND dialog) {
    HDC dc = ::GetDC(dialog);
    const int dpi = dc != nullptr ? ::GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc != nullptr) ::ReleaseDC(dialog, dc);
    const int pad = ::MulDiv(4, dpi, 96);
    ::EnumChildWindows(
        dialog,
        [](HWND child, LPARAM value) -> BOOL {
            wchar_t name[16]{};
            ::GetClassNameW(child, name, 16);
            if (_wcsicmp(name, L"Edit") == 0) {
                ::SendMessageW(child, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                               MAKELPARAM(value, value));
            }
            return TRUE;
        },
        pad);
}

//! titleformat_help.html ships with foobar2000, next to the executable.
void open_titleformat_help(HWND owner) {
    wchar_t module[MAX_PATH]{};
    const DWORD length = ::GetModuleFileNameW(nullptr, module, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return;
    std::wstring path(module, length);
    const std::size_t slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos) return;
    path.erase(slash + 1);
    path += L"doc\\titleformat_help.html";
    ::ShellExecuteW(owner, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

//! Fills a drop-down in enum order, so the selection index *is* the stored value.
void fill_combo(HWND combo, std::initializer_list<const wchar_t*> items) {
    if (combo == nullptr) return;
    ::SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const wchar_t* item : items) ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
}

[[nodiscard]] bool is_hex(wchar_t c) {
    return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F');
}

//! The icon field: "E8D6", "U+E8D6", "0xE8D6", or the character itself. 0 when empty, nothing
//! when it is neither.
[[nodiscard]] std::optional<std::uint32_t> parse_icon(std::wstring text) {
    while (!text.empty() && iswspace(text.front()) != 0) text.erase(0, 1);
    while (!text.empty() && iswspace(text.back()) != 0) text.pop_back();
    if (text.empty()) return 0u;
    // One character (two UTF-16 units for a surrogate pair) is the glyph itself.
    if (text.size() == 1 && !(text[0] >= 0xD800 && text[0] <= 0xDFFF)) return static_cast<std::uint32_t>(text[0]);
    if (text.size() == 2 && text[0] >= 0xD800 && text[0] <= 0xDBFF && text[1] >= 0xDC00 && text[1] <= 0xDFFF) {
        return 0x10000u + ((static_cast<std::uint32_t>(text[0]) - 0xD800u) << 10) +
               (static_cast<std::uint32_t>(text[1]) - 0xDC00u);
    }
    std::wstring digits = text;
    if (digits.size() > 2 && (digits[0] == L'U' || digits[0] == L'u') && digits[1] == L'+') digits.erase(0, 2);
    else if (digits.size() > 2 && digits[0] == L'0' && (digits[1] == L'x' || digits[1] == L'X')) digits.erase(0, 2);
    if (digits.empty() || digits.size() > 6) return std::nullopt;
    std::uint32_t value = 0;
    for (const wchar_t c : digits) {
        if (!is_hex(c)) return std::nullopt;
        value = (value << 4) | static_cast<std::uint32_t>(c <= L'9' ? c - L'0' : (c | 0x20) - L'a' + 10);
    }
    if (value == 0 || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return std::nullopt;
    return value;
}

[[nodiscard]] std::wstring format_icon(std::uint32_t cp) {
    if (cp == 0) return {};
    wchar_t buffer[12]{};
    std::swprintf(buffer, 12, L"%04X", cp);
    return buffer;
}

[[nodiscard]] std::wstring glyph(std::uint32_t cp) {
    if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return {};
    if (cp < 0x10000) return std::wstring(1, static_cast<wchar_t>(cp));
    cp -= 0x10000;
    return std::wstring{static_cast<wchar_t>(0xD800 + (cp >> 10)), static_cast<wchar_t>(0xDC00 + (cp & 0x3FF))};
}

[[nodiscard]] bool private_use(std::uint32_t cp) {
    return (cp >= 0xE000 && cp <= 0xF8FF) || cp >= 0xF0000;
}

int CALLBACK family_found(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM found) {
    *reinterpret_cast<bool*>(found) = true;
    return 0;
}

[[nodiscard]] bool font_installed(const wchar_t* family) {
    LOGFONTW probe{};
    probe.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(probe.lfFaceName, family, _TRUNCATE);
    bool found = false;
    if (HDC screen = ::GetDC(nullptr)) {
        ::EnumFontFamiliesExW(screen, &probe, &family_found, reinterpret_cast<LPARAM>(&found), 0);
        ::ReleaseDC(nullptr, screen);
    }
    return found;
}

//! The same choice the strip makes for Private Use Area icons.
[[nodiscard]] const wchar_t* system_icon_family() {
    for (const wchar_t* family : {L"Segoe Fluent Icons", L"Segoe MDL2 Assets"}) {
        if (font_installed(family)) return family;
    }
    return L"Segoe UI Symbol";
}

[[nodiscard]] HFONT make_font(HWND wnd, const wchar_t* family, int points) {
    HDC dc = ::GetDC(wnd);
    const int dpi = dc != nullptr ? ::GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc != nullptr) ::ReleaseDC(wnd, dc);
    LOGFONTW lf{};
    lf.lfHeight = -::MulDiv(points, dpi, 72);
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcsncpy_s(lf.lfFaceName, family, _TRUNCATE);
    return ::CreateFontIndirectW(&lf);
}

//! An owner-drawn colour swatch on its page's own background (right in light and dark mode).
void draw_swatch(const DRAWITEMSTRUCT& item, COLORREF colour) {
    const HDC dc = item.hDC;
    const HWND page = ::GetParent(item.hwndItem);
    auto brush = reinterpret_cast<HBRUSH>(
        ::SendMessageW(page, WM_CTLCOLORDLG, reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(page)));
    ::FillRect(dc, &item.rcItem, brush != nullptr ? brush : ::GetSysColorBrush(COLOR_BTNFACE));
    const bool dark = DarkMode::IsDialogDark(page);
    RECT swatch = item.rcItem;
    ::InflateRect(&swatch, -1, -1);
    COLORREF fill = colour;
    if ((item.itemState & ODS_DISABLED) != 0) {
        const COLORREF back = dark ? RGB(32, 32, 32) : ::GetSysColor(COLOR_BTNFACE);
        fill = RGB((GetRValue(fill) + 2 * GetRValue(back)) / 3, (GetGValue(fill) + 2 * GetGValue(back)) / 3,
                   (GetBValue(fill) + 2 * GetBValue(back)) / 3);
    }
    const HBRUSH fill_brush = ::CreateSolidBrush(fill);
    ::FillRect(dc, &swatch, fill_brush);
    ::DeleteObject(fill_brush);
    const HBRUSH frame = ::CreateSolidBrush(dark ? RGB(130, 130, 130) : ::GetSysColor(COLOR_BTNSHADOW));
    ::FrameRect(dc, &swatch, frame);
    ::DeleteObject(frame);
    if ((item.itemState & ODS_FOCUS) != 0 && (item.itemState & ODS_NOFOCUSRECT) == 0) {
        ::DrawFocusRect(dc, &item.rcItem);
    }
}

//! The system colour picker; false if cancelled.
[[nodiscard]] bool pick_colour(HWND owner, std::uint32_t& argb) {
    static COLORREF custom[16]{};
    CHOOSECOLORW dialog{};
    dialog.lStructSize = sizeof dialog;
    dialog.hwndOwner = owner;
    dialog.rgbResult = colorref(argb);
    dialog.lpCustColors = custom;
    dialog.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!::ChooseColorW(&dialog)) return false;
    const COLORREF c = dialog.rgbResult;
    argb = 0xFF000000u | (static_cast<std::uint32_t>(GetRValue(c)) << 16) |
           (static_cast<std::uint32_t>(GetGValue(c)) << 8) | GetBValue(c);
    return true;
}

// ---------------------------------------------------------------------------------------------

class ConfigureDialog : public CDialogImpl<ConfigureDialog> {
public:
    enum { IDD = IDD_CONFIGURE };

    ConfigureDialog(ConfigureState& state, ConfigureTarget& target, bool live)
        : state_(state), target_(target), live_(live) {}

    BEGIN_MSG_MAP_EX(ConfigureDialog)
        MSG_WM_INITDIALOG(on_init_dialog)
        MSG_WM_DESTROY(on_destroy)
        MSG_WM_COMMAND(on_command)
        MSG_WM_DRAWITEM(on_draw_item)
        MESSAGE_HANDLER_EX(WM_HSCROLL, on_hscroll)
        MESSAGE_HANDLER_EX(WM_NOTIFY, on_notify)
    END_MSG_MAP()

private:
    BOOL on_init_dialog(CWindow, LPARAM);
    void on_destroy();
    void on_command(UINT code, int id, CWindow control);
    void on_draw_item(UINT, LPDRAWITEMSTRUCT item);
    LRESULT on_hscroll(UINT, WPARAM, LPARAM);
    //! TCN_SELCHANGE. Raw WM_NOTIFY: dialogs have been sent WM_NOTIFY with an lParam that is no
    //! pointer at all, which crashes a handler that reads the header.
    LRESULT on_notify(UINT, WPARAM, LPARAM lparam);

    void create_pages();
    //! Pages keep nothing themselves: commands, owner-draw and scroll messages go to the dialog.
    static INT_PTR CALLBACK page_proc(HWND, UINT, WPARAM, LPARAM);
    void show_page(int page);

    [[nodiscard]] HWND control(int id) const { return find_control(m_hWnd, id); }
    [[nodiscard]] bool checked(int id) const {
        return ::SendMessageW(control(id), BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    void check(int id, bool on) const {
        ::SendMessageW(control(id), BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    void enable(int id, bool on) const { ::EnableWindow(control(id), on ? TRUE : FALSE); }
    [[nodiscard]] LRESULT selection(int id) const { return ::SendMessageW(control(id), CB_GETCURSEL, 0, 0); }
    void select(int id, int index) const { ::SendMessageW(control(id), CB_SETCURSEL, static_cast<WPARAM>(index), 0); }
    [[nodiscard]] std::uint16_t number(int id) const;
    void set_number(int id, unsigned value) const { ::SetDlgItemInt(::GetParent(control(id)), id, value, FALSE); }
    [[nodiscard]] int slider(int id) const {
        return static_cast<int>(::SendMessageW(control(id), TBM_GETPOS, 0, 0));
    }
    void set_slider(int id, int lo, int hi, int value) const;

    void settings_to_controls();
    void settings_from_controls();
    void update_values();
    void update_enabled();

    void fill_tab_list();
    [[nodiscard]] std::wstring tab_line(const TabEdit& tab) const;
    void select_tab(int index);
    void tab_to_controls();
    void tab_from_controls(int id);
    void update_icon_preview();
    void move_selected(int delta);
    //! Removes the selected tab (deleted when the dialog closes with OK).
    void remove_selected();

    void changed();

    ConfigureState& state_;
    ConfigureTarget& target_;
    const bool live_;
    bool loading_{false};
    int selected_{-1};
    HWND pages_[page_count]{};
    HFONT icon_font_{nullptr};
    HFONT emoji_font_{nullptr};
    // Must be a member: it hooks the dialog and its controls for the lifetime of both.
    fb2k::CDarkModeHooks dark_;
};

BOOL ConfigureDialog::on_init_dialog(CWindow, LPARAM) {
    dark_.AddDialogWithControls(*this);
    create_pages();
    {
        const HWND tabs = ::GetDlgItem(m_hWnd, IDC_TABS);
        const wchar_t* const names[page_count] = {L"Strip", L"Look", L"Tabs", L"Behaviour", L"Auto-hide"};
        for (int i = 0; i < page_count; ++i) {
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            item.pszText = const_cast<wchar_t*>(names[i]);
            ::SendMessageW(tabs, TCM_INSERTITEMW, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(&item));
        }
        // Only the strip is wanted, not an empty page frame under it.
        RECT item{};
        RECT client{};
        ::SendMessageW(tabs, TCM_GETITEMRECT, 0, reinterpret_cast<LPARAM>(&item));
        ::GetClientRect(tabs, &client);
        ::SetWindowPos(tabs, nullptr, 0, 0, client.right, item.bottom + 2, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        show_page(0);
    }
    fill_combo(control(IDC_POSITION), {L"Top", L"Bottom", L"Left", L"Right"});
    // Order matters: visibility_order below.
    fill_combo(control(IDC_VISIBILITY), {L"Always", L"Only with two or more tabs", L"Auto-hide", L"Never"});
    // Order matters: RevealMode and ShowHideAnimation, one for one.
    fill_combo(control(IDC_AH_MODE), {L"Over the panel (fastest)", L"Push the panel aside"});
    fill_combo(control(IDC_AH_ANIM), {L"None", L"Slide", L"Fade"});
    fill_combo(control(IDC_SIZING), {L"Fit the title", L"All equal", L"Fill the strip"});
    fill_combo(control(IDC_ALIGN), {L"Start", L"Centre", L"End"});
    fill_combo(control(IDC_INDICATOR), {L"Underline", L"Pill", L"Text only"});
    // Order matters: AccentSource and StripBackground, one for one.
    const std::wstring ui = state_.ui_name != nullptr ? state_.ui_name : L"Columns UI";
    const std::wstring ui_selection = ui + L" selection colour";
    const std::wstring ui_background = ui + L" background";
    fill_combo(control(IDC_ACCENT_SOURCE), {ui_selection.c_str(), L"Custom colour", L"From the playing cover"});
    fill_combo(control(IDC_BACKGROUND), {ui_background.c_str(), L"Custom colour", L"Tinted with the accent"});
    fill_combo(control(IDC_MIDDLE), {L"Does nothing", L"Hides the tab"});

    icon_font_ = make_font(m_hWnd, system_icon_family(), 12);
    emoji_font_ = make_font(m_hWnd, L"Segoe UI Emoji", 12);

    // One tab stop in the tab list: panel, then title.
    const int stop = 110;
    ::SendMessageW(control(IDC_TAB_LIST), LB_SETTABSTOPS, 1, reinterpret_cast<LPARAM>(&stop));

    settings_to_controls();
    // After the dark-mode hooks (they may replace list controls).
    fill_tab_list();
    select_tab(state_.tabs.empty() ? -1 : 0);
    return TRUE;
}

void ConfigureDialog::on_destroy() {
    if (icon_font_ != nullptr) ::DeleteObject(icon_font_);
    if (emoji_font_ != nullptr) ::DeleteObject(emoji_font_);
    icon_font_ = emoji_font_ = nullptr;
    SetMsgHandled(FALSE);
}

void ConfigureDialog::create_pages() {
    RECT host{};
    ::GetWindowRect(::GetDlgItem(m_hWnd, IDC_PAGE_HOST), &host);
    ::MapWindowPoints(nullptr, m_hWnd, reinterpret_cast<POINT*>(&host), 2);
    for (int i = 0; i < page_count; ++i) {
        const HWND page = ::CreateDialogParamW(core_api::get_my_instance(), MAKEINTRESOURCEW(IDD_PAGE_STRIP + i),
                                               m_hWnd, &ConfigureDialog::page_proc, 0);
        pages_[i] = page;
        if (page == nullptr) continue;
        ::SetWindowPos(page, ::GetDlgItem(m_hWnd, IDC_PAGE_HOST), host.left, host.top, host.right - host.left,
                       host.bottom - host.top, SWP_NOACTIVATE);
        dark_.AddDialogWithControls(page);
        pad_text_fields(page);
    }
}

INT_PTR CALLBACK ConfigureDialog::page_proc(HWND page, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_INITDIALOG: return FALSE;
    case WM_COMMAND:
    case WM_DRAWITEM:
    case WM_HSCROLL: ::SendMessageW(::GetParent(page), message, wparam, lparam); return TRUE;
    default: return FALSE;
    }
}

LRESULT ConfigureDialog::on_notify(UINT, WPARAM, LPARAM lparam) {
    SetMsgHandled(FALSE);
    if (lparam < 0x10000) return 0;
    const auto* header = reinterpret_cast<const NMHDR*>(lparam);
    if (header->idFrom == IDC_TABS && header->code == TCN_SELCHANGE) {
        show_page(static_cast<int>(::SendMessageW(header->hwndFrom, TCM_GETCURSEL, 0, 0)));
        SetMsgHandled(TRUE);
    }
    return 0;
}

void ConfigureDialog::show_page(int page) {
    for (int i = 0; i < page_count; ++i) {
        if (pages_[i] != nullptr) ::ShowWindow(pages_[i], i == page ? SW_SHOWNA : SW_HIDE);
    }
}

std::uint16_t ConfigureDialog::number(int id) const {
    BOOL ok = FALSE;
    const UINT value = ::GetDlgItemInt(::GetParent(control(id)), id, &ok, FALSE);
    return static_cast<std::uint16_t>(ok ? (std::min)(value, 0xFFFFu) : 0u);
}

void ConfigureDialog::set_slider(int id, int lo, int hi, int value) const {
    const HWND bar = control(id);
    ::SendMessageW(bar, TBM_SETRANGE, TRUE, MAKELPARAM(lo, hi));
    ::SendMessageW(bar, TBM_SETPAGESIZE, 0, 5);
    ::SendMessageW(bar, TBM_SETPOS, TRUE, value);
}

void ConfigureDialog::settings_to_controls() {
    loading_ = true;
    const Settings& s = state_.settings;
    select(IDC_POSITION, static_cast<int>(s.position));
    select(IDC_VISIBILITY, s.visibility == StripVisibility::always        ? 0
                           : s.visibility == StripVisibility::two_or_more ? 1
                           : s.visibility == StripVisibility::auto_hide   ? 2
                                                                          : 3);
    check(IDC_ROTATE, s.side_text == SideText::rotated);
    select(IDC_SIZING, static_cast<int>(s.sizing));
    select(IDC_ALIGN, static_cast<int>(s.align));
    set_number(IDC_PAD_X, s.pad_x);
    set_number(IDC_PAD_Y, s.pad_y);
    set_number(IDC_SPACING, s.spacing);
    set_number(IDC_THICKNESS, s.thickness);
    set_number(IDC_MAX_WIDTH, s.max_tab_width);

    select(IDC_INDICATOR, static_cast<int>(s.indicator));
    check(IDC_CHIP, s.chip);
    set_number(IDC_RADIUS, s.corner_radius);
    check(IDC_STRENGTH_AUTO, s.accent_strength == 0);
    set_slider(IDC_STRENGTH, 5, 100, s.accent_strength == 0 ? 35 : s.accent_strength);
    select(IDC_ACCENT_SOURCE, static_cast<int>(s.accent_source));
    ::SetWindowTextW(control(IDC_ACCENT_HEX), format_rgb(s.accent_argb).c_str());
    select(IDC_BACKGROUND, static_cast<int>(s.strip_background));
    ::SetWindowTextW(control(IDC_BACKGROUND_HEX), format_rgb(s.background_argb).c_str());
    set_slider(IDC_TINT, 2, 60, s.tint_strength);

    check(IDC_WHEEL, s.wheel_cycles);
    check(IDC_DRAG, s.drag_reorder);
    check(IDC_CTRL_TAB, s.ctrl_tab);
    select(IDC_MIDDLE, s.middle_click == MiddleClick::nothing ? 0 : 1);
    check(IDC_LAZY, s.lazy_children);
    check(IDC_REMEMBER, s.remember_active);
    check(IDC_ICONS_ONLY, s.icons_only);
    check(IDC_SWITCH_ANIM, s.animations);
    set_number(IDC_SWITCH_MS, s.switch_ms);

    select(IDC_AH_MODE, static_cast<int>(s.reveal_mode));
    select(IDC_AH_ANIM, static_cast<int>(s.show_hide_animation));
    set_number(IDC_AH_ANIM_MS, s.animation_ms);
    set_number(IDC_AH_HOT_ZONE, s.hot_zone);
    set_number(IDC_AH_REVEAL, s.reveal_delay_ms);
    set_number(IDC_AH_HIDE, s.hide_delay_ms);
    set_number(IDC_AH_LINGER, s.linger_ms);
    loading_ = false;
    update_values();
    update_enabled();
    ::InvalidateRect(control(IDC_ACCENT_SWATCH), nullptr, FALSE);
    ::InvalidateRect(control(IDC_BACKGROUND_SWATCH), nullptr, FALSE);
}

void ConfigureDialog::settings_from_controls() {
    Settings& s = state_.settings;
    const auto pick = [&](int id, auto& field) {
        const LRESULT index = selection(id);
        if (index != CB_ERR) field = static_cast<std::remove_reference_t<decltype(field)>>(index);
    };
    pick(IDC_POSITION, s.position);
    if (const LRESULT v = selection(IDC_VISIBILITY); v != CB_ERR) {
        constexpr StripVisibility visibility_order[] = {StripVisibility::always, StripVisibility::two_or_more,
                                                        StripVisibility::auto_hide, StripVisibility::never};
        if (v >= 0 && v < 4) s.visibility = visibility_order[v];
    }
    s.side_text = checked(IDC_ROTATE) ? SideText::rotated : SideText::horizontal;
    pick(IDC_SIZING, s.sizing);
    pick(IDC_ALIGN, s.align);
    s.pad_x = number(IDC_PAD_X);
    s.pad_y = number(IDC_PAD_Y);
    s.spacing = number(IDC_SPACING);
    s.thickness = number(IDC_THICKNESS);
    s.max_tab_width = number(IDC_MAX_WIDTH);

    pick(IDC_INDICATOR, s.indicator);
    s.chip = checked(IDC_CHIP);
    s.corner_radius = number(IDC_RADIUS);
    s.accent_strength = checked(IDC_STRENGTH_AUTO) ? 0 : static_cast<std::uint8_t>(std::clamp(slider(IDC_STRENGTH), 5, 100));
    pick(IDC_ACCENT_SOURCE, s.accent_source);
    s.accent_argb = parse_rgb(window_text(control(IDC_ACCENT_HEX)), s.accent_argb);
    pick(IDC_BACKGROUND, s.strip_background);
    s.background_argb = parse_rgb(window_text(control(IDC_BACKGROUND_HEX)), s.background_argb);
    s.tint_strength = static_cast<std::uint8_t>(std::clamp(slider(IDC_TINT), 2, 60));

    s.wheel_cycles = checked(IDC_WHEEL);
    s.drag_reorder = checked(IDC_DRAG);
    s.ctrl_tab = checked(IDC_CTRL_TAB);
    if (const LRESULT v = selection(IDC_MIDDLE); v != CB_ERR) {
        s.middle_click = v == 0 ? MiddleClick::nothing : MiddleClick::hide_tab;
    }
    s.lazy_children = checked(IDC_LAZY);
    s.remember_active = checked(IDC_REMEMBER);
    s.icons_only = checked(IDC_ICONS_ONLY);
    s.animations = checked(IDC_SWITCH_ANIM);
    s.switch_ms = number(IDC_SWITCH_MS);

    pick(IDC_AH_MODE, s.reveal_mode);
    pick(IDC_AH_ANIM, s.show_hide_animation);
    s.animation_ms = number(IDC_AH_ANIM_MS);
    s.hot_zone = number(IDC_AH_HOT_ZONE);
    s.reveal_delay_ms = number(IDC_AH_REVEAL);
    s.hide_delay_ms = number(IDC_AH_HIDE);
    s.linger_ms = number(IDC_AH_LINGER);
}

void ConfigureDialog::update_values() {
    wchar_t text[16]{};
    std::swprintf(text, 16, L"%d%%", slider(IDC_STRENGTH));
    ::SetWindowTextW(control(IDC_STRENGTH_VALUE), checked(IDC_STRENGTH_AUTO) ? L"" : text);
    std::swprintf(text, 16, L"%d%%", slider(IDC_TINT));
    ::SetWindowTextW(control(IDC_TINT_VALUE), text);
}

void ConfigureDialog::update_enabled() {
    const Settings& s = state_.settings;
    enable(IDC_ROTATE, s.position == StripPosition::left || s.position == StripPosition::right);
    enable(IDC_ALIGN, s.sizing != TabSizing::fill);
    // The strength is the opacity of a fill, which only the pill and chips have.
    const bool fill = s.indicator == Indicator::pill || s.chip;
    enable(IDC_STRENGTH_AUTO, fill);
    enable(IDC_STRENGTH, fill && s.accent_strength != 0);
    enable(IDC_STRENGTH_VALUE, fill && s.accent_strength != 0);
    const bool custom_accent = s.accent_source == AccentSource::custom;
    enable(IDC_ACCENT_HEX, custom_accent);
    enable(IDC_ACCENT_SWATCH, custom_accent);
    const bool custom_background = s.strip_background == StripBackground::custom;
    enable(IDC_BACKGROUND_HEX, custom_background);
    enable(IDC_BACKGROUND_SWATCH, custom_background);
    const bool tint = s.strip_background == StripBackground::accent_tint;
    enable(IDC_TINT, tint);
    enable(IDC_TINT_VALUE, tint);

    enable(IDC_SWITCH_MS, s.animations);

    const bool auto_hide = s.visibility == StripVisibility::auto_hide;
    for (const int id : {IDC_AH_MODE, IDC_AH_ANIM, IDC_AH_HOT_ZONE, IDC_AH_REVEAL, IDC_AH_HIDE, IDC_AH_LINGER}) {
        enable(id, auto_hide);
    }
    // Show/hide animations run on the layered strip, i.e. over the panel only.
    enable(IDC_AH_ANIM, auto_hide && s.reveal_mode == RevealMode::overlay);
    enable(IDC_AH_ANIM_MS, auto_hide && s.reveal_mode == RevealMode::overlay &&
                               s.show_hide_animation != ShowHideAnimation::none);

    const bool have = selected_ >= 0 && static_cast<std::size_t>(selected_) < state_.tabs.size();
    for (const int id : {IDC_TAB_TITLE, IDC_TAB_TITLE_HELP, IDC_TAB_FORMAT, IDC_TAB_ICON, IDC_CHARMAP,
                         IDC_TAB_HIDDEN, IDC_TAB_ON_PLAY, IDC_TAB_ON_STOP}) {
        enable(id, have);
    }
    enable(IDC_REMOVE, live_ && have);
    enable(IDC_MOVE_UP, live_ && have && selected_ > 0);
    enable(IDC_MOVE_DOWN, live_ && have && static_cast<std::size_t>(selected_) + 1 < state_.tabs.size());
}

std::wstring ConfigureDialog::tab_line(const TabEdit& tab) const {
    std::wstring line = tab.panel_name;
    if (tab.extra.hidden) line += L" (hidden)";
    line += L'\t';
    if (tab.extra.use_custom_title) line += to_wide(tab.extra.title);
    return line;
}

void ConfigureDialog::fill_tab_list() {
    const HWND list = control(IDC_TAB_LIST);
    ::SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    ::SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const TabEdit& tab : state_.tabs) {
        ::SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(tab_line(tab).c_str()));
    }
    ::SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    ::InvalidateRect(list, nullptr, TRUE);
}

void ConfigureDialog::select_tab(int index) {
    selected_ = index >= 0 && static_cast<std::size_t>(index) < state_.tabs.size() ? index : -1;
    ::SendMessageW(control(IDC_TAB_LIST), LB_SETCURSEL, static_cast<WPARAM>(selected_), 0);
    tab_to_controls();
    update_enabled();
}

void ConfigureDialog::tab_to_controls() {
    loading_ = true;
    TabExtra none;
    const TabExtra& e = selected_ >= 0 ? state_.tabs[static_cast<std::size_t>(selected_)].extra : none;
    ::SetWindowTextW(control(IDC_TAB_TITLE), e.use_custom_title ? to_wide(e.title).c_str() : L"");
    if (selected_ >= 0) {
        ::SendMessageW(control(IDC_TAB_TITLE), EM_SETCUEBANNER, TRUE,
                       reinterpret_cast<LPARAM>(state_.tabs[static_cast<std::size_t>(selected_)].panel_name.c_str()));
    }
    check(IDC_TAB_FORMAT, e.title_is_format);
    ::SetWindowTextW(control(IDC_TAB_ICON), format_icon(e.icon).c_str());
    check(IDC_TAB_HIDDEN, e.hidden);
    check(IDC_TAB_ON_PLAY, e.show_on_play);
    check(IDC_TAB_ON_STOP, e.show_on_stop);
    loading_ = false;
    update_icon_preview();
}

void ConfigureDialog::tab_from_controls(int id) {
    if (selected_ < 0) return;
    const auto index = static_cast<std::size_t>(selected_);
    TabExtra& e = state_.tabs[index].extra;
    const std::wstring title = window_text(control(IDC_TAB_TITLE));
    e.use_custom_title = !title.empty();
    if (!title.empty() || id == IDC_TAB_TITLE) e.title = to_utf8(title);
    e.title_is_format = checked(IDC_TAB_FORMAT);
    if (const std::optional<std::uint32_t> icon = parse_icon(window_text(control(IDC_TAB_ICON)))) e.icon = *icon;
    if (id == IDC_TAB_HIDDEN && checked(IDC_TAB_HIDDEN)) {
        // Keep one tab: a container with every tab hidden could only be repaired from here.
        bool other = false;
        for (std::size_t i = 0; i < state_.tabs.size(); ++i) other = other || (i != index && !state_.tabs[i].extra.hidden);
        if (!other) {
            check(IDC_TAB_HIDDEN, false);
            ::MessageBeep(MB_ICONWARNING);
        }
    }
    e.hidden = checked(IDC_TAB_HIDDEN);
    e.show_on_play = checked(IDC_TAB_ON_PLAY);
    e.show_on_stop = checked(IDC_TAB_ON_STOP);
    // One tab per event: the last one ticked.
    for (std::size_t i = 0; i < state_.tabs.size(); ++i) {
        if (i == index) continue;
        if (id == IDC_TAB_ON_PLAY && e.show_on_play) state_.tabs[i].extra.show_on_play = false;
        if (id == IDC_TAB_ON_STOP && e.show_on_stop) state_.tabs[i].extra.show_on_stop = false;
    }

    const HWND list = control(IDC_TAB_LIST);
    ::SendMessageW(list, LB_DELETESTRING, index, 0);
    ::SendMessageW(list, LB_INSERTSTRING, index, reinterpret_cast<LPARAM>(tab_line(state_.tabs[index]).c_str()));
    ::SendMessageW(list, LB_SETCURSEL, index, 0);
    if (id == IDC_TAB_ICON) update_icon_preview();
}

void ConfigureDialog::update_icon_preview() {
    const HWND preview = control(IDC_TAB_ICON_PREVIEW);
    if (preview == nullptr) return;
    const std::optional<std::uint32_t> icon =
        selected_ >= 0 ? parse_icon(window_text(control(IDC_TAB_ICON))) : std::optional<std::uint32_t>(0u);
    const HFONT font = icon && private_use(*icon) ? icon_font_ : emoji_font_;
    if (font != nullptr) ::SendMessageW(preview, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    ::SetWindowTextW(preview, !icon ? L"?" : glyph(*icon).c_str());
}

void ConfigureDialog::move_selected(int delta) {
    if (!live_ || selected_ < 0) return;
    const int target = selected_ + delta;
    if (target < 0 || static_cast<std::size_t>(target) >= state_.tabs.size()) return;
    std::swap(state_.tabs[static_cast<std::size_t>(selected_)], state_.tabs[static_cast<std::size_t>(target)]);
    fill_tab_list();
    select_tab(target);
    changed();
}

void ConfigureDialog::remove_selected() {
    if (!live_ || selected_ < 0 || static_cast<std::size_t>(selected_) >= state_.tabs.size()) return;
    state_.tabs.erase(state_.tabs.begin() + selected_);
    const int next = (std::min)(selected_, static_cast<int>(state_.tabs.size()) - 1);
    fill_tab_list();
    select_tab(next);
    changed();
}

void ConfigureDialog::changed() {
    update_values();
    update_enabled();
    if (live_) target_.preview(state_);
}

void ConfigureDialog::on_command(UINT code, int id, CWindow) {
    switch (id) {
    case IDOK:
        settings_from_controls();
        EndDialog(IDOK);
        return;
    case IDCANCEL: EndDialog(IDCANCEL); return;
    default: break;
    }
    if (loading_) return;
    switch (id) {
    case IDC_DEFAULTS:
        state_.settings = Settings{};
        settings_to_controls();
        changed();
        return;
    case IDC_ACCENT_SWATCH:
    case IDC_BACKGROUND_SWATCH: {
        if (code != BN_CLICKED) return;
        const int hex = id == IDC_ACCENT_SWATCH ? IDC_ACCENT_HEX : IDC_BACKGROUND_HEX;
        std::uint32_t argb = id == IDC_ACCENT_SWATCH ? state_.settings.accent_argb : state_.settings.background_argb;
        // Fires EN_CHANGE, and with it the usual change handling.
        if (pick_colour(m_hWnd, argb)) ::SetWindowTextW(control(hex), format_rgb(argb).c_str());
        return;
    }
    case IDC_TAB_TITLE_HELP:
        if (code == BN_CLICKED) open_titleformat_help(m_hWnd);
        return;
    case IDC_REMOVE:
        if (code == BN_CLICKED) remove_selected();
        return;
    case IDC_CHARMAP:
        if (code == BN_CLICKED) {
            // Debloated Windows installs drop charmap.exe. Then Microsoft's Segoe Fluent Icons
            // page, which lists every icon with its code point (most are shared with MDL2).
            const auto open = [this](const wchar_t* file) {
                SHELLEXECUTEINFOW info{};
                info.cbSize = sizeof(info);
                info.fMask = SEE_MASK_FLAG_NO_UI;
                info.hwnd = m_hWnd;
                info.lpVerb = L"open";
                info.lpFile = file;
                info.nShow = SW_SHOWNORMAL;
                return ::ShellExecuteExW(&info) != FALSE;
            };
            wchar_t found[MAX_PATH]{};
            const bool have_charmap = ::SearchPathW(nullptr, L"charmap.exe", nullptr, MAX_PATH, found, nullptr) != 0;
            if (!have_charmap || !open(found)) {
                (void)open(L"https://learn.microsoft.com/windows/apps/design/style/segoe-fluent-icons-font");
            }
        }
        return;
    case IDC_MOVE_UP:
    case IDC_MOVE_DOWN:
        if (code == BN_CLICKED) move_selected(id == IDC_MOVE_UP ? -1 : 1);
        return;
    case IDC_TAB_LIST:
        if (code == LBN_SELCHANGE) {
            select_tab(static_cast<int>(::SendMessageW(control(IDC_TAB_LIST), LB_GETCURSEL, 0, 0)));
        }
        return;
    case IDC_TAB_TITLE:
    case IDC_TAB_ICON:
        if (code == EN_CHANGE) {
            tab_from_controls(id);
            changed();
        }
        return;
    case IDC_TAB_FORMAT:
    case IDC_TAB_HIDDEN:
    case IDC_TAB_ON_PLAY:
    case IDC_TAB_ON_STOP:
        if (code == BN_CLICKED) {
            tab_from_controls(id);
            changed();
        }
        return;
    default: break;
    }
    if (code == EN_CHANGE || code == BN_CLICKED || code == CBN_SELCHANGE) {
        settings_from_controls();
        if (id == IDC_ACCENT_HEX) ::InvalidateRect(control(IDC_ACCENT_SWATCH), nullptr, FALSE);
        if (id == IDC_BACKGROUND_HEX) ::InvalidateRect(control(IDC_BACKGROUND_SWATCH), nullptr, FALSE);
        changed();
    }
}

LRESULT ConfigureDialog::on_hscroll(UINT, WPARAM, LPARAM lparam) {
    const auto bar = reinterpret_cast<HWND>(lparam);
    if (loading_ || bar == nullptr || (bar != control(IDC_STRENGTH) && bar != control(IDC_TINT))) {
        SetMsgHandled(FALSE);
        return 0;
    }
    settings_from_controls();
    changed();
    return 0;
}

void ConfigureDialog::on_draw_item(UINT, LPDRAWITEMSTRUCT item) {
    if (item == nullptr || (item->CtlID != IDC_ACCENT_SWATCH && item->CtlID != IDC_BACKGROUND_SWATCH)) {
        SetMsgHandled(FALSE);
        return;
    }
    const bool accent = item->CtlID == IDC_ACCENT_SWATCH;
    const std::uint32_t argb =
        parse_rgb(window_text(control(accent ? IDC_ACCENT_HEX : IDC_BACKGROUND_HEX)),
                  accent ? state_.settings.accent_argb : state_.settings.background_argb);
    draw_swatch(*item, colorref(argb));
}

// ---------------------------------------------------------------------------------------------

class RenameDialog : public CDialogImpl<RenameDialog> {
public:
    enum { IDD = IDD_RENAME };

    RenameDialog(const std::wstring& panel_name, TabExtra& extra) : panel_name_(panel_name), extra_(extra) {}

    BEGIN_MSG_MAP_EX(RenameDialog)
        MSG_WM_INITDIALOG(on_init_dialog)
        COMMAND_ID_HANDLER_EX(IDOK, on_ok)
        COMMAND_ID_HANDLER_EX(IDCANCEL, on_cancel)
        COMMAND_ID_HANDLER_EX(IDC_RENAME_HELP, on_help)
    END_MSG_MAP()

private:
    BOOL on_init_dialog(CWindow, LPARAM) {
        dark_.AddDialogWithControls(*this);
        pad_text_fields(m_hWnd);
        const HWND edit = GetDlgItem(IDC_RENAME_TITLE);
        ::SetWindowTextW(edit, extra_.use_custom_title ? to_wide(extra_.title).c_str() : L"");
        ::SendMessageW(edit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(panel_name_.c_str()));
        ::SendMessageW(edit, EM_SETSEL, 0, -1);
        CheckDlgButton(IDC_RENAME_FORMAT, extra_.title_is_format ? BST_CHECKED : BST_UNCHECKED);
        ::SetFocus(edit);
        return FALSE; // focus set here
    }
    void on_ok(UINT, int, CWindow) {
        const std::wstring title = window_text(GetDlgItem(IDC_RENAME_TITLE));
        extra_.use_custom_title = !title.empty();
        if (!title.empty()) extra_.title = to_utf8(title);
        extra_.title_is_format = IsDlgButtonChecked(IDC_RENAME_FORMAT) == BST_CHECKED;
        EndDialog(IDOK);
    }
    void on_cancel(UINT, int, CWindow) { EndDialog(IDCANCEL); }
    void on_help(UINT, int, CWindow) { open_titleformat_help(m_hWnd); }

    const std::wstring& panel_name_;
    TabExtra& extra_;
    fb2k::CDarkModeHooks dark_;
};

} // namespace

bool run_configure_dialog(HWND parent, ConfigureState& state, ConfigureTarget& target, bool live) {
    ConfigureDialog dialog(state, target, live);
    return dialog.DoModal(parent) == IDOK;
}

bool run_rename_dialog(HWND parent, const std::wstring& panel_name, TabExtra& extra) {
    RenameDialog dialog(panel_name, extra);
    return dialog.DoModal(parent) == IDOK;
}

} // namespace bettertabs
