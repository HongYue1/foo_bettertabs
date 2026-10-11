// The Configure dialog and the Rename dialog. Layout: foo_bettertabs.rc (style profile at the
// top). Conventions: one child dialog per page with its own dark-mode hooks, guarded WM_NOTIFY,
// padded edits, swatch + hex colours, tooltips from the tips table.

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

#include "fbc/fonts.h"

#include "../../resource.h"
#include "configure_dialog.h"

namespace bettertabs {

namespace {

constexpr int page_count = 10;
//! In tab order, with their tab names.
constexpr int page_ids[page_count] = {IDD_PAGE_STRIP,  IDD_PAGE_LOOK,      IDD_PAGE_HOVER,  IDD_PAGE_COLOURS,
                                      IDD_PAGE_FONTS,  IDD_PAGE_TABS,      IDD_PAGE_ANIMATION,
                                      IDD_PAGE_PANELS, IDD_PAGE_INPUT,     IDD_PAGE_VISIBILITY};
constexpr const wchar_t* page_names[page_count] = {L"Strip", L"Look", L"Hover",     L"Colours", L"Fonts",
                                                   L"Tabs",  L"Animation", L"Panels", L"Input", L"Visibility"};
//! Where an automatic line width's edit rests (the width it draws at 96 DPI).
[[nodiscard]] int auto_line_width(bool outline) noexcept { return outline ? 1 : 2; }
//! Where a hover fill slider rests while Automatic is ticked.
[[nodiscard]] int auto_hover_fill_for(bool active) noexcept { return active ? auto_active_hover_fill : auto_hover_fill; }

//! Tooltips: a control's tip also shows on its label (the static before it, ending in a colon).
//! Ranges, what 0 or Automatic means, and anything not clear at first glance live here, so the
//! pages keep short labels and bare units.
struct Tip {
    int id;
    const wchar_t* text;
};
constexpr const wchar_t* tip_hex = L"The colour as hex RRGGBB. Click the swatch to pick one.";
constexpr const wchar_t* tip_swatch = L"Pick a colour.";
constexpr const wchar_t* tip_hover_colour = L"Text colour: a neutral wash. Accent colour: the accent from the Colours page.";
constexpr const wchar_t* tip_hover_fill_auto = L"Automatic: 18% on the other tabs, 23% on the active tab.";
constexpr const wchar_t* tip_hover_fill = L"How strong the hover fill is (2-100%).";
constexpr const wchar_t* tip_hover_width_auto = L"Automatic: 2 px for an underline, thinner for an outline.";
constexpr const wchar_t* tip_hover_width = L"Width of the outline or underline (1-8 px).";
constexpr const wchar_t* tip_hover_line_auto = L"Automatic: 50% in the text colour, full strength in any other colour.";
constexpr const wchar_t* tip_hover_line = L"How strong the outline or underline is (10-100%).";
constexpr const wchar_t* tip_fallback =
    L"Tried in this order for characters the tab font cannot draw. Only the family is used: size, weight "
    L"and italic follow the tab font. The built-in fallbacks (emoji, then Windows') come after these.";
constexpr const wchar_t* tip_font =
    L"Default follows the Columns UI font for these tabs (Colours and fonts), or the Default UI tab font. "
    L"Picked sizes are exact; tab icons keep their icon font.";
constexpr const wchar_t* tip_ms = L"Length of the animation (50-1000 ms).";
constexpr Tip tips[] = {
    {IDC_DEFAULTS, L"Puts every setting on every page back to its default. Cancel still undoes it."},
    // Strip
    {IDC_POSITION, L"The edge of the panel the strip sits on."},
    {IDC_THICKNESS, L"Height of a top or bottom strip, width of a side strip. 0 = from the font, or 1-200 px."},
    {IDC_ROTATE, L"On a left or right strip, titles run along the strip. Off: titles stay level and the strip grows "
                 L"as wide as its longest title."},
    {IDC_SIZING, L"Fit the title: each tab as wide as its title. All equal: every tab as wide as the widest. "
                 L"Fill the strip: the tabs share all of its length."},
    {IDC_ALIGN, L"Where the tabs sit when they do not fill the strip."},
    {IDC_CHEVRON, L"Where the chevron for the tabs that do not fit sits."},
    {IDC_SHRINK, L"When the tabs do not fit, the longest titles get shorter (with an ellipsis) before tabs move to the "
                 L"chevron. Off: titles stay whole."},
    {IDC_MAX_WIDTH, L"Longer titles are cut with an ellipsis and shown whole in a tooltip. 0 = no limit, or 1-2000 px."},
    {IDC_PAD_X, L"Space before and after the title (0-64 px)."},
    {IDC_PAD_Y, L"Space above and below the title (0-64 px)."},
    {IDC_SPACING, L"Gap between two tabs (0-64 px)."},
    // Look
    {IDC_CHIP, L"Every tab gets a rounded fill of its own; the active tab's stands out."},
    {IDC_CHIP_COLOUR, L"Text colour: a neutral wash. Accent colour: the accent from the Colours page."},
    {IDC_CHIP_HEX, tip_hex},
    {IDC_CHIP_SWATCH, tip_swatch},
    {IDC_CHIP_STRENGTH_AUTO, L"Automatic: 18% on a dark strip, 15% on a light one."},
    {IDC_CHIP_STRENGTH, L"How strong the chips' fill is (2-100%)."},
    {IDC_RADIUS, L"Rounding of chips, pills and tabs (0-32 px)."},
    {IDC_ICONS_ONLY, L"Tabs with an icon (Tabs page) show just the icon; the title is the tooltip."},
    {IDC_INDICATOR, L"How the active tab is marked. Tab: a fill joined to the panel. Outlined tab: the same shape, "
                    L"outlined. Text only: just its brighter title."},
    {IDC_LINE_WIDTH_AUTO, L"Automatic: 2 px for the underline, thinner for the outline."},
    {IDC_LINE_WIDTH, L"Width of the underline or the outline (1-8 px)."},
    {IDC_STRENGTH_AUTO, L"Automatic: 50% on a dark strip, 40% on a light one."},
    {IDC_STRENGTH, L"How strong the active tab's pill, tab or chip fill is (2-100%)."},
    // Hover
    {IDC_HOVER_STYLE, L"How a tab is marked while the pointer is over it."},
    {IDC_HOVER_ACTIVE_STYLE, L"How the active tab is marked while the pointer is over it. Plain wash: a faint "
                             L"wash in the text colour."},
    {IDC_HOVER_COLOUR, tip_hover_colour},
    {IDC_HOVER_ACTIVE_COLOUR, tip_hover_colour},
    {IDC_HOVER_HEX, tip_hex},
    {IDC_HOVER_ACTIVE_HEX, tip_hex},
    {IDC_HOVER_SWATCH, tip_swatch},
    {IDC_HOVER_ACTIVE_SWATCH, tip_swatch},
    {IDC_HOVER_TEXT, L"What the title does on hover. Brightens: takes the active tab's text colour."},
    {IDC_HOVER_ACTIVE_TEXT, L"What the active tab's title does on hover. Brightens: gets lighter, towards white."},
    {IDC_HOVER_TEXT_HEX, L"Used with Title: Custom colour. Hex RRGGBB; click the swatch to pick one."},
    {IDC_HOVER_ACTIVE_TEXT_HEX, L"Used with Title: Custom colour. Hex RRGGBB; click the swatch to pick one."},
    {IDC_HOVER_TEXT_SWATCH, tip_swatch},
    {IDC_HOVER_ACTIVE_TEXT_SWATCH, tip_swatch},
    {IDC_HOVER_FILL_AUTO, tip_hover_fill_auto},
    {IDC_HOVER_ACTIVE_FILL_AUTO, tip_hover_fill_auto},
    {IDC_HOVER_FILL, tip_hover_fill},
    {IDC_HOVER_ACTIVE_FILL, tip_hover_fill},
    {IDC_HOVER_LINE_WIDTH_AUTO, tip_hover_width_auto},
    {IDC_HOVER_ACTIVE_LINE_WIDTH_AUTO, tip_hover_width_auto},
    {IDC_HOVER_LINE_WIDTH, tip_hover_width},
    {IDC_HOVER_ACTIVE_LINE_WIDTH, tip_hover_width},
    {IDC_HOVER_LINE_AUTO, tip_hover_line_auto},
    {IDC_HOVER_ACTIVE_LINE_AUTO, tip_hover_line_auto},
    {IDC_HOVER_LINE, tip_hover_line},
    {IDC_HOVER_ACTIVE_LINE, tip_hover_line},
    // Colours
    {IDC_ACCENT_SOURCE, L"The colour of the active tab's mark. From the playing cover: taken from the playing "
                        L"track's cover art."},
    {IDC_ACCENT_HEX, tip_hex},
    {IDC_ACCENT_SWATCH, tip_swatch},
    {IDC_TEXT_CUSTOM, L"Your own colour for the other tabs' titles, used exactly as picked. Off: the theme's text, "
                      L"dimmed."},
    {IDC_TEXT_HEX, tip_hex},
    {IDC_TEXT_SWATCH, tip_swatch},
    {IDC_ACTIVE_TEXT_CUSTOM, L"Your own colour for the active and selected tabs' titles, used exactly as picked. "
                             L"Off: the theme's text."},
    {IDC_ACTIVE_TEXT_HEX, tip_hex},
    {IDC_ACTIVE_TEXT_SWATCH, tip_swatch},
    {IDC_BACKGROUND, L"Tinted with the accent: the theme's background with some of the accent mixed in."},
    {IDC_BACKGROUND_HEX, tip_hex},
    {IDC_BACKGROUND_SWATCH, tip_swatch},
    {IDC_TINT, L"How much accent goes into a tinted background (2-100%)."},
    {IDC_TRANSPARENT, L"The strip shows the layout's own background, such as a Columns UI theme's image; tabs keep "
                      L"their fills. Not while auto-hide shows the strip over the panel."},
    {IDC_TRANSPARENT_OPACITY, L"How much of the strip's own background still covers the layout's (0-100%, 0 = fully "
                              L"transparent)."},
    // Fonts
    {IDC_FONT_TEXT, tip_font},
    {IDC_FONT_PICK, tip_font},
    {IDC_FONT_CLEAR, tip_font},
    {IDC_FALLBACK_TEXT, tip_fallback},
    {IDC_FALLBACK_TEXT + 1, tip_fallback},
    {IDC_FALLBACK_TEXT + 2, tip_fallback},
    {IDC_FALLBACK_PICK, tip_fallback},
    {IDC_FALLBACK_PICK + 1, tip_fallback},
    {IDC_FALLBACK_PICK + 2, tip_fallback},
    {IDC_FALLBACK_CLEAR, tip_fallback},
    {IDC_FALLBACK_CLEAR + 1, tip_fallback},
    {IDC_FALLBACK_CLEAR + 2, tip_fallback},
    // Tabs
    {IDC_TAB_LIST, L"Every panel in this container, in strip order, with its own title if it has one."},
    {IDC_REMOVE, L"Removes the tab and its panel when you click OK."},
    {IDC_TAB_TITLE, L"Your title for the tab. Empty: the panel's own name."},
    {IDC_TAB_TITLE_HELP, L"Opens foobar2000's title formatting help."},
    {IDC_TAB_FORMAT, L"The title is a title formatting pattern."},
    {IDC_TAB_ICON, L"A code point (such as E8D6) or the character itself. Icons: Segoe Fluent Icons. Emoji: Win+. "
                   L"(period)."},
    {IDC_CHARMAP, L"Opens Character Map to copy a Segoe Fluent Icons glyph (or Microsoft's icon list when Character "
                  L"Map is missing)."},
    {IDC_TAB_HIDDEN, L"The tab leaves the strip; Show hidden tab in the strip menu brings it back."},
    {IDC_TAB_ON_PLAY, L"This tab becomes the active one when playback starts."},
    {IDC_TAB_ON_STOP, L"This tab becomes the active one when playback stops."},
    // Animation
    {IDC_SWITCH_ANIM, L"The active tab's mark slides over to the new tab."},
    {IDC_SWITCH_MS, tip_ms},
    {IDC_HOVER_FADE, L"The hover mark fades in and out."},
    {IDC_HOVER_FADE_MS, tip_ms},
    {IDC_AH_ANIM, L"How an auto-hiding strip appears and goes."},
    {IDC_AH_ANIM_MS, tip_ms},
    // Panels
    {IDC_LAZY, L"A panel is created the first time its tab is shown, for a faster startup. Off: all panels are "
               L"created at once."},
    {IDC_REMEMBER, L"The tab that was active last time is active again at startup. Off: the first tab."},
    // Input
    {IDC_MIDDLE, L"Hide tab: the tab leaves the strip; Show hidden tab in the strip menu brings it back."},
    {IDC_WHEEL, L"The wheel over the strip switches to the next or previous tab."},
    {IDC_DRAG, L"Drag a tab to move it; a selected tab brings the whole selection along. Esc cancels."},
    {IDC_CTRL_TAB, L"Ctrl+Tab and Ctrl+Shift+Tab, while the keyboard focus is inside the container."},
    // Visibility
    {IDC_VISIBILITY, L"Only with two or more tabs: no strip for a single tab. Auto-hide: the strip shows when the "
                     L"pointer reaches its edge."},
    {IDC_AH_MODE, L"Over the panel: the strip covers the edge of the panel. Push the panel aside: the panel makes room."},
    {IDC_AH_HOT_ZONE, L"How close to the edge the pointer has to come (1-32 px)."},
    {IDC_AH_REVEAL, L"Time in the hot zone before the strip shows (0-5000 ms)."},
    {IDC_AH_HIDE, L"Time after the pointer leaves before the strip hides (0-5000 ms). It also stays while its menu is "
                  L"open, during a drag and while it has the keyboard focus."},
    {IDC_AH_LINGER, L"How long the strip stays at least after a tab switch (0-5000 ms)."},
};

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

//! Text fields get some room before the first character (fb2k-common).
using fbc::fonts::pad_text_fields;

[[nodiscard]] fbc::fonts::FontChoice font_choice(const TabFont& font) {
    return {font.family, font.tenths_pt, font.weight, font.italic};
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

//! A colour swatch button, its hex field and the setting they edit.
struct Swatch {
    int button;
    int hex;
    std::uint32_t Settings::*field;
};
constexpr Swatch swatches[] = {
    {IDC_ACCENT_SWATCH, IDC_ACCENT_HEX, &Settings::accent_argb},
    {IDC_BACKGROUND_SWATCH, IDC_BACKGROUND_HEX, &Settings::background_argb},
    {IDC_HOVER_SWATCH, IDC_HOVER_HEX, &Settings::hover_argb},
    {IDC_HOVER_TEXT_SWATCH, IDC_HOVER_TEXT_HEX, &Settings::hover_text_argb},
    {IDC_HOVER_ACTIVE_SWATCH, IDC_HOVER_ACTIVE_HEX, &Settings::active_hover_argb},
    {IDC_HOVER_ACTIVE_TEXT_SWATCH, IDC_HOVER_ACTIVE_TEXT_HEX, &Settings::active_hover_text_argb},
    {IDC_TEXT_SWATCH, IDC_TEXT_HEX, &Settings::text_argb},
    {IDC_ACTIVE_TEXT_SWATCH, IDC_ACTIVE_TEXT_HEX, &Settings::active_text_argb},
    {IDC_CHIP_SWATCH, IDC_CHIP_HEX, &Settings::chip_argb},
};
//! The Hover page shows two sets side by side: the other tabs' (Settings::hover_*) and the
//! active tab's (Settings::active_hover_*).
struct HoverFields {
    HoverStyle Settings::*style;
    HoverColour Settings::*colour;
    std::uint32_t Settings::*argb;
    std::uint8_t Settings::*fill_strength;
    std::uint8_t Settings::*line_width;
    std::uint8_t Settings::*line_opacity;
    HoverText Settings::*text;
    std::uint32_t Settings::*text_argb;
};
constexpr HoverFields hover_others{&Settings::hover_style,         &Settings::hover_colour,
                                   &Settings::hover_argb,          &Settings::hover_fill_strength,
                                   &Settings::hover_line_width,    &Settings::hover_line_opacity,
                                   &Settings::hover_text,          &Settings::hover_text_argb};
constexpr HoverFields hover_active{&Settings::active_hover_style,         &Settings::active_hover_colour,
                                   &Settings::active_hover_argb,          &Settings::active_hover_fill_strength,
                                   &Settings::active_hover_line_width,    &Settings::active_hover_line_opacity,
                                   &Settings::active_hover_text,          &Settings::active_hover_text_argb};
//! One column of the Hover page.
struct HoverIds {
    int style, colour, hex, swatch, text, text_hex, text_swatch;
    int fill_auto, fill, fill_value, line_width_auto, line_width, line_auto, line, line_value;
};
struct HoverSet {
    const HoverFields& fields;
    HoverIds ids;
    bool active;
};
constexpr HoverSet hover_sets[] = {
    {hover_others,
     {IDC_HOVER_STYLE, IDC_HOVER_COLOUR, IDC_HOVER_HEX, IDC_HOVER_SWATCH, IDC_HOVER_TEXT, IDC_HOVER_TEXT_HEX,
      IDC_HOVER_TEXT_SWATCH, IDC_HOVER_FILL_AUTO, IDC_HOVER_FILL, IDC_HOVER_FILL_VALUE, IDC_HOVER_LINE_WIDTH_AUTO, IDC_HOVER_LINE_WIDTH,
      IDC_HOVER_LINE_AUTO, IDC_HOVER_LINE, IDC_HOVER_LINE_VALUE},
     false},
    {hover_active,
     {IDC_HOVER_ACTIVE_STYLE, IDC_HOVER_ACTIVE_COLOUR, IDC_HOVER_ACTIVE_HEX, IDC_HOVER_ACTIVE_SWATCH,
      IDC_HOVER_ACTIVE_TEXT, IDC_HOVER_ACTIVE_TEXT_HEX, IDC_HOVER_ACTIVE_TEXT_SWATCH, IDC_HOVER_ACTIVE_FILL_AUTO,
      IDC_HOVER_ACTIVE_FILL, IDC_HOVER_ACTIVE_FILL_VALUE, IDC_HOVER_ACTIVE_LINE_WIDTH_AUTO, IDC_HOVER_ACTIVE_LINE_WIDTH, IDC_HOVER_ACTIVE_LINE_AUTO,
      IDC_HOVER_ACTIVE_LINE, IDC_HOVER_ACTIVE_LINE_VALUE},
     true},
};
//! The style list: the HoverStyle values in order; the active tab's starts with its plain wash.
//! The hover mark's line is an outline (else an underline).
[[nodiscard]] bool hover_outline(HoverStyle style) noexcept {
    return style == HoverStyle::outline || style == HoverStyle::outline_fill;
}
[[nodiscard]] int hover_style_index(HoverStyle style, bool active) noexcept {
    if (!active) return style == HoverStyle::plain ? 0 : static_cast<int>(style);
    return style == HoverStyle::plain ? 0 : static_cast<int>(style) + 1;
}
[[nodiscard]] HoverStyle hover_style_at(LRESULT index, bool active) noexcept {
    if (active) {
        if (index == 0) return HoverStyle::plain;
        --index;
    }
    return index >= 0 && index <= static_cast<LRESULT>(HoverStyle::none) ? static_cast<HoverStyle>(index) : HoverStyle::fill;
}
//! `button` must be one of the swatches; anything else gets the first.
[[nodiscard]] const Swatch& swatch_for(int button) {
    for (const Swatch& swatch : swatches) {
        if (swatch.button == button) return swatch;
    }
    return swatches[0];
}

// ---------------------------------------------------------------------------------------------

class ConfigureDialog : public CDialogImpl<ConfigureDialog> {
public:
    enum { IDD = IDD_CONFIGURE };

    //! Modal (DoModal): edits `state` in place.
    ConfigureDialog(ConfigureState& state, ConfigureTarget& target, bool live)
        : state_(state), target_(target), live_(live), modeless_(false) {}
    //! Modeless (open_configure_dialog): edits its own copy, always live, and tells `target` how it
    //! closed. Deletes itself.
    ConfigureDialog(const ConfigureState& state, ConfigureTarget& target)
        : own_(state), state_(own_), target_(target), live_(true), modeless_(true) {}

    //! Closes a modeless dialog without telling the target.
    static constexpr UINT wm_close_quietly = WM_APP + 1;

    BEGIN_MSG_MAP_EX(ConfigureDialog)
        MSG_WM_INITDIALOG(on_init_dialog)
        MSG_WM_DESTROY(on_destroy)
        MESSAGE_HANDLER_EX(wm_close_quietly, on_close_quietly)
        MSG_WM_COMMAND(on_command)
        MSG_WM_DRAWITEM(on_draw_item)
        MESSAGE_HANDLER_EX(WM_HSCROLL, on_hscroll)
        MESSAGE_HANDLER_EX(WM_NOTIFY, on_notify)
    END_MSG_MAP()

    void OnFinalMessage(HWND) override {
        if (modeless_) delete this;
    }

private:
    BOOL on_init_dialog(CWindow, LPARAM);
    void on_destroy();
    LRESULT on_close_quietly(UINT, WPARAM, LPARAM);
    //! OK or Cancel: modal, ends the dialog; modeless, tells the target and destroys the window.
    void finish(bool ok);
    void on_command(UINT code, int id, CWindow control);
    void on_draw_item(UINT, LPDRAWITEMSTRUCT item);
    LRESULT on_hscroll(UINT, WPARAM, LPARAM);
    //! TCN_SELCHANGE. Raw WM_NOTIFY: dialogs have been sent WM_NOTIFY with an lParam that is no
    //! pointer at all, which crashes a handler that reads the header.
    LRESULT on_notify(UINT, WPARAM, LPARAM lparam);

    void create_pages();
    //! One tooltip window for the tips table, on the controls and their labels.
    void create_tips();
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
    //! Enables a control; on a change, repaints the page behind it (see repaint_behind).
    void enable(int id, bool on) const {
        const HWND w = control(id);
        if (w == nullptr || (::IsWindowEnabled(w) != FALSE) == on) return;
        ::EnableWindow(w, on ? TRUE : FALSE);
        repaint_behind(w);
    }
    //! Sets a label's text only when it changes, then repaints the page behind it.
    void set_label(int id, const wchar_t* text) const {
        const HWND w = control(id);
        if (w == nullptr) return;
        wchar_t current[256]{};
        ::GetWindowTextW(w, current, 256);
        if (std::wcscmp(current, text) == 0) return;
        ::SetWindowTextW(w, text);
        repaint_behind(w);
    }
    //! Dark mode draws static text on a transparent background, so a static repainted on its own
    //! (new text, enabled or disabled) draws over its old glyphs, which pile up into a bold,
    //! fringed look. Erasing the page behind it first draws it once, cleanly.
    static void repaint_behind(HWND w) {
        const HWND page = ::GetParent(w);
        RECT r{};
        ::GetWindowRect(w, &r);
        ::MapWindowPoints(HWND_DESKTOP, page, reinterpret_cast<POINT*>(&r), 2);
        ::RedrawWindow(page, &r, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }
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
    //! The Fonts page's rows, from state_.settings.font.
    void show_fonts();
    //! Select, Default and Clear on the Fonts page. False for any other control.
    bool on_font_command(int id);

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

    //! The modeless dialog's state (state_ refers to it); unused when modal.
    ConfigureState own_;
    ConfigureState& state_;
    ConfigureTarget& target_;
    const bool live_;
    const bool modeless_;
    //! The target knows how the modeless dialog closed (or must not hear of it).
    bool told_{false};
    bool loading_{false};
    void fill_hover_styles(const HoverSet& set);
    void hover_to_controls(const HoverSet& set);
    void hover_from_controls(const HoverSet& set);
    void hover_values(const HoverSet& set);
    void hover_enabled(const HoverSet& set);
    int selected_{-1};
    HFONT icon_font_{nullptr};
    HFONT emoji_font_{nullptr};
    HWND pages_[page_count]{};
    HWND tips_{};
    // Must be a member: it hooks the dialog and its controls for the lifetime of both.
    fb2k::CDarkModeHooks dark_;
};

BOOL ConfigureDialog::on_init_dialog(CWindow, LPARAM) {
    dark_.AddDialogWithControls(*this);
    create_pages();
    create_tips();
    {
        const HWND tabs = ::GetDlgItem(m_hWnd, IDC_TABS);
        for (int i = 0; i < page_count; ++i) {
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            item.pszText = const_cast<wchar_t*>(page_names[i]);
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
    // Order matters: ChevronPosition.
    fill_combo(control(IDC_CHEVRON), {L"At the end of the strip", L"At the start of the strip"});
    // Order matters: Indicator, one for one.
    fill_combo(control(IDC_INDICATOR), {L"Underline", L"Pill", L"Text only", L"Tab", L"Outlined tab"});
    // Order matters: ChipColour.
    fill_combo(control(IDC_CHIP_COLOUR), {L"Text colour", L"Accent colour", L"Custom colour"});
    for (const HoverSet& set : hover_sets) {
        fill_hover_styles(set);
        // Order matters: HoverColour and HoverText, one for one.
        fill_combo(control(set.ids.colour), {L"Text colour", L"Accent colour", L"Custom colour"});
        fill_combo(control(set.ids.text), {L"Brightens", L"Stays as it is", L"Takes the hover colour", L"Custom colour"});
    }
    // Order matters: AccentSource and StripBackground, one for one.
    const std::wstring ui = state_.ui_name != nullptr ? state_.ui_name : L"Columns UI";
    const std::wstring ui_selection = ui + L" selection colour";
    const std::wstring ui_background = ui + L" background";
    const std::wstring ui_highlight = ui + L" " + (state_.highlight_name != nullptr ? state_.highlight_name : L"");
    fill_combo(control(IDC_ACCENT_SOURCE),
               {ui_selection.c_str(), L"Custom colour", L"From the playing cover", ui_highlight.c_str()});
    fill_combo(control(IDC_BACKGROUND), {ui_background.c_str(), L"Custom colour", L"Tinted with the accent"});
    fill_combo(control(IDC_MIDDLE), {L"None", L"Hide tab"});

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

void ConfigureDialog::create_pages() {
    RECT host{};
    ::GetWindowRect(::GetDlgItem(m_hWnd, IDC_PAGE_HOST), &host);
    ::MapWindowPoints(nullptr, m_hWnd, reinterpret_cast<POINT*>(&host), 2);
    for (int i = 0; i < page_count; ++i) {
        const HWND page = ::CreateDialogParamW(core_api::get_my_instance(), MAKEINTRESOURCEW(page_ids[i]),
                                               m_hWnd, &ConfigureDialog::page_proc, 0);
        pages_[i] = page;
        if (page == nullptr) continue;
        ::SetWindowPos(page, ::GetDlgItem(m_hWnd, IDC_PAGE_HOST), host.left, host.top, host.right - host.left,
                       host.bottom - host.top, SWP_NOACTIVATE);
        dark_.AddDialogWithControls(page);
        pad_text_fields(page);
    }
}

void ConfigureDialog::create_tips() {
    tips_ = ::CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                              CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, m_hWnd, nullptr,
                              core_api::get_my_instance(), nullptr);
    if (tips_ == nullptr) return;
    RECT wrap{0, 0, 200, 0};
    ::MapDialogRect(m_hWnd, &wrap);
    ::SendMessageW(tips_, TTM_SETMAXTIPWIDTH, 0, wrap.right);
    ::SendMessageW(tips_, TTM_SETDELAYTIME, TTDT_AUTOPOP, 30000);
    DarkMode::ApplyDarkThemeCtrl(tips_, DarkMode::IsDialogDark(m_hWnd));
    UINT_PTR area = 1;
    // A disabled control gets no mouse messages and a label lets them through: its page gets them,
    // so both are also areas of the page.
    const auto add_area = [&](HWND page, HWND w, const wchar_t* text) {
        TTTOOLINFOW info{};
        info.cbSize = sizeof(info);
        info.uFlags = TTF_SUBCLASS;
        info.hwnd = page;
        info.uId = area++;
        ::GetWindowRect(w, &info.rect);
        ::MapWindowPoints(HWND_DESKTOP, page, reinterpret_cast<POINT*>(&info.rect), 2);
        info.lpszText = const_cast<wchar_t*>(text);
        ::SendMessageW(tips_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
    };
    for (const Tip& tip : tips) {
        const HWND w = control(tip.id);
        if (w == nullptr) continue;
        const HWND page = ::GetParent(w);
        TTTOOLINFOW info{};
        info.cbSize = sizeof(info);
        info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        info.hwnd = page;
        info.uId = reinterpret_cast<UINT_PTR>(w);
        info.lpszText = const_cast<wchar_t*>(tip.text);
        ::SendMessageW(tips_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
        add_area(page, w, tip.text);
        const HWND label = ::GetWindow(w, GW_HWNDPREV);
        wchar_t name[16]{};
        wchar_t text[64]{};
        if (label == nullptr || ::GetClassNameW(label, name, 16) == 0 || std::wcscmp(name, L"Static") != 0) continue;
        const int length = ::GetWindowTextW(label, text, 64);
        if (length > 0 && text[length - 1] == L':') add_area(page, label, tip.text);
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
    select(IDC_CHEVRON, static_cast<int>(s.chevron_position));
    check(IDC_SHRINK, s.shrink_titles);
    set_number(IDC_PAD_X, s.pad_x);
    set_number(IDC_PAD_Y, s.pad_y);
    set_number(IDC_SPACING, s.spacing);
    set_number(IDC_THICKNESS, s.thickness);
    set_number(IDC_MAX_WIDTH, s.max_tab_width);

    select(IDC_INDICATOR, static_cast<int>(s.indicator));
    check(IDC_LINE_WIDTH_AUTO, s.line_width == 0);
    set_number(IDC_LINE_WIDTH, s.line_width != 0 ? s.line_width : auto_line_width(false));
    check(IDC_CHIP, s.chip);
    select(IDC_CHIP_COLOUR, static_cast<int>(s.chip_colour));
    ::SetWindowTextW(control(IDC_CHIP_HEX), format_rgb(s.chip_argb).c_str());
    check(IDC_CHIP_STRENGTH_AUTO, s.chip_strength == 0);
    set_slider(IDC_CHIP_STRENGTH, 2, 100,
               s.chip_strength != 0 ? s.chip_strength : (state_.dark ? auto_chip_dark : auto_chip_light));
    set_number(IDC_RADIUS, s.corner_radius);
    check(IDC_STRENGTH_AUTO, s.accent_strength == 0);
    set_slider(IDC_STRENGTH, 2, 100,
               s.accent_strength != 0 ? s.accent_strength : (state_.dark ? auto_fill_dark : auto_fill_light));
    select(IDC_ACCENT_SOURCE, static_cast<int>(s.accent_source));
    ::SetWindowTextW(control(IDC_ACCENT_HEX), format_rgb(s.accent_argb).c_str());
    select(IDC_BACKGROUND, static_cast<int>(s.strip_background));
    ::SetWindowTextW(control(IDC_BACKGROUND_HEX), format_rgb(s.background_argb).c_str());
    set_slider(IDC_TINT, 2, 100, s.tint_strength);
    check(IDC_TRANSPARENT, s.transparent_background);
    set_slider(IDC_TRANSPARENT_OPACITY, 0, 100, s.transparent_opacity);
    check(IDC_TEXT_CUSTOM, s.custom_text);
    ::SetWindowTextW(control(IDC_TEXT_HEX), format_rgb(s.text_argb).c_str());
    check(IDC_ACTIVE_TEXT_CUSTOM, s.custom_active_text);
    ::SetWindowTextW(control(IDC_ACTIVE_TEXT_HEX), format_rgb(s.active_text_argb).c_str());

    for (const HoverSet& set : hover_sets) hover_to_controls(set);
    check(IDC_HOVER_FADE, s.hover_fade);
    set_number(IDC_HOVER_FADE_MS, s.hover_fade_ms);

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
    show_fonts();
    loading_ = false;
    update_values();
    update_enabled();
    for (const Swatch& swatch : swatches) ::InvalidateRect(control(swatch.button), nullptr, FALSE);
}

void ConfigureDialog::fill_hover_styles(const HoverSet& set) {
    std::vector<const wchar_t*> names;
    if (set.active) names.push_back(L"Plain wash");
    // Order matters: HoverStyle, one for one (see hover_style_index).
    for (const wchar_t* name : {L"Fill", L"Outline", L"Outline and fill", L"Underline", L"Underline and fill", L"No mark"}) {
        names.push_back(name);
    }
    const HWND combo = control(set.ids.style);
    ::SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const wchar_t* name : names) ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
}

void ConfigureDialog::hover_to_controls(const HoverSet& set) {
    const Settings& s = state_.settings;
    const HoverFields& h = set.fields;
    const HoverIds& id = set.ids;
    select(id.style, hover_style_index(s.*h.style, set.active));
    select(id.colour, static_cast<int>(s.*h.colour));
    ::SetWindowTextW(control(id.hex), format_rgb(s.*h.argb).c_str());
    check(id.fill_auto, s.*h.fill_strength == 0);
    set_slider(id.fill, 2, 100, s.*h.fill_strength == 0 ? auto_hover_fill_for(set.active) : s.*h.fill_strength);
    check(id.line_width_auto, s.*h.line_width == 0);
    set_number(id.line_width, s.*h.line_width != 0 ? s.*h.line_width : auto_line_width(hover_outline(s.*h.style)));
    check(id.line_auto, s.*h.line_opacity == 0);
    set_slider(id.line, 10, 100, s.*h.line_opacity == 0 ? 50 : s.*h.line_opacity);
    // "Brightens" is the full text colour for the others, lighter (towards white) for the active tab.
    select(id.text, static_cast<int>(s.*h.text));
    ::SetWindowTextW(control(id.text_hex), format_rgb(s.*h.text_argb).c_str());
}

void ConfigureDialog::hover_from_controls(const HoverSet& set) {
    Settings& s = state_.settings;
    const HoverFields& h = set.fields;
    const HoverIds& id = set.ids;
    if (const LRESULT index = selection(id.style); index != CB_ERR) s.*h.style = hover_style_at(index, set.active);
    if (const LRESULT index = selection(id.colour); index != CB_ERR) s.*h.colour = static_cast<HoverColour>(index);
    s.*h.argb = parse_rgb(window_text(control(id.hex)), s.*h.argb);
    s.*h.fill_strength = checked(id.fill_auto) ? 0 : static_cast<std::uint8_t>(std::clamp(slider(id.fill), 2, 100));
    s.*h.line_width =
        checked(id.line_width_auto) ? 0 : static_cast<std::uint8_t>(std::clamp<std::uint16_t>(number(id.line_width), 1, 8));
    s.*h.line_opacity = checked(id.line_auto) ? 0 : static_cast<std::uint8_t>(std::clamp(slider(id.line), 10, 100));
    if (const LRESULT index = selection(id.text); index != CB_ERR) s.*h.text = static_cast<HoverText>(index);
    s.*h.text_argb = parse_rgb(window_text(control(id.text_hex)), s.*h.text_argb);
}

void ConfigureDialog::hover_values(const HoverSet& set) {
    wchar_t text[16]{};
    std::swprintf(text, 16, L"%d%%", slider(set.ids.fill));
    set_label(set.ids.fill_value, checked(set.ids.fill_auto) ? L"" : text);
    std::swprintf(text, 16, L"%d%%", slider(set.ids.line));
    set_label(set.ids.line_value, checked(set.ids.line_auto) ? L"" : text);
}

//! Each control only where the chosen style uses it.
void ConfigureDialog::hover_enabled(const HoverSet& set) {
    const Settings& s = state_.settings;
    const HoverFields& h = set.fields;
    const HoverIds& id = set.ids;
    const HoverStyle hs = s.*h.style;
    const bool hover_fill = hs == HoverStyle::fill || hs == HoverStyle::outline_fill || hs == HoverStyle::underline_fill;
    const bool hover_line = hs == HoverStyle::outline || hs == HoverStyle::outline_fill ||
                            hs == HoverStyle::underline || hs == HoverStyle::underline_fill;
    // The plain wash is always the text colour.
    const bool hover_tinted = (hs != HoverStyle::none && hs != HoverStyle::plain) || s.*h.text == HoverText::colour;
    enable(id.colour, hover_tinted);
    const bool custom_hover = hover_tinted && s.*h.colour == HoverColour::custom;
    enable(id.hex, custom_hover);
    enable(id.swatch, custom_hover);
    enable(id.text_hex, s.*h.text == HoverText::custom);
    enable(id.text_swatch, s.*h.text == HoverText::custom);
    enable(id.fill_auto, hover_fill);
    enable(id.fill, hover_fill && s.*h.fill_strength != 0);
    enable(id.fill_value, hover_fill && s.*h.fill_strength != 0);
    enable(id.line_width_auto, hover_line);
    enable(id.line_width, hover_line && s.*h.line_width != 0);
    enable(id.line_auto, hover_line);
    enable(id.line, hover_line && s.*h.line_opacity != 0);
    enable(id.line_value, hover_line && s.*h.line_opacity != 0);
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
    pick(IDC_CHEVRON, s.chevron_position);
    s.shrink_titles = checked(IDC_SHRINK);
    s.pad_x = number(IDC_PAD_X);
    s.pad_y = number(IDC_PAD_Y);
    s.spacing = number(IDC_SPACING);
    s.thickness = number(IDC_THICKNESS);
    s.max_tab_width = number(IDC_MAX_WIDTH);

    pick(IDC_INDICATOR, s.indicator);
    s.line_width = checked(IDC_LINE_WIDTH_AUTO)
                       ? 0
                       : static_cast<std::uint8_t>(std::clamp<std::uint16_t>(number(IDC_LINE_WIDTH), 1, 8));
    s.chip = checked(IDC_CHIP);
    pick(IDC_CHIP_COLOUR, s.chip_colour);
    s.chip_argb = parse_rgb(window_text(control(IDC_CHIP_HEX)), s.chip_argb);
    s.chip_strength =
        checked(IDC_CHIP_STRENGTH_AUTO) ? 0 : static_cast<std::uint8_t>(std::clamp(slider(IDC_CHIP_STRENGTH), 2, 100));
    s.corner_radius = number(IDC_RADIUS);
    s.accent_strength = checked(IDC_STRENGTH_AUTO) ? 0 : static_cast<std::uint8_t>(std::clamp(slider(IDC_STRENGTH), 2, 100));
    pick(IDC_ACCENT_SOURCE, s.accent_source);
    s.accent_argb = parse_rgb(window_text(control(IDC_ACCENT_HEX)), s.accent_argb);
    pick(IDC_BACKGROUND, s.strip_background);
    s.background_argb = parse_rgb(window_text(control(IDC_BACKGROUND_HEX)), s.background_argb);
    s.tint_strength = static_cast<std::uint8_t>(std::clamp(slider(IDC_TINT), 2, 100));
    s.transparent_background = checked(IDC_TRANSPARENT);
    s.transparent_opacity = static_cast<std::uint8_t>(std::clamp(slider(IDC_TRANSPARENT_OPACITY), 0, 100));
    s.custom_text = checked(IDC_TEXT_CUSTOM);
    s.text_argb = parse_rgb(window_text(control(IDC_TEXT_HEX)), s.text_argb);
    s.custom_active_text = checked(IDC_ACTIVE_TEXT_CUSTOM);
    s.active_text_argb = parse_rgb(window_text(control(IDC_ACTIVE_TEXT_HEX)), s.active_text_argb);

    for (const HoverSet& set : hover_sets) hover_from_controls(set);
    s.hover_fade = checked(IDC_HOVER_FADE);
    s.hover_fade_ms = number(IDC_HOVER_FADE_MS);

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
    set_label(IDC_STRENGTH_VALUE, checked(IDC_STRENGTH_AUTO) ? L"" : text);
    std::swprintf(text, 16, L"%d%%", slider(IDC_CHIP_STRENGTH));
    set_label(IDC_CHIP_STRENGTH_VALUE, checked(IDC_CHIP_STRENGTH_AUTO) ? L"" : text);
    std::swprintf(text, 16, L"%d%%", slider(IDC_TINT));
    set_label(IDC_TINT_VALUE, text);
    std::swprintf(text, 16, L"%d%%", slider(IDC_TRANSPARENT_OPACITY));
    set_label(IDC_TRANSPARENT_OPACITY_VALUE, text);
    for (const HoverSet& set : hover_sets) hover_values(set);
}

void ConfigureDialog::show_fonts() {
    const TabFont& f = state_.settings.font;
    const std::string text =
        f.family.empty()
            ? fbc::fonts::describe_default({fbc::fonts::narrow(state_.host_font_family), state_.host_font_tenths})
            : fbc::fonts::describe(font_choice(f));
    set_label(IDC_FONT_TEXT, fbc::fonts::widen(text).c_str());
    for (int i = 0; i < static_cast<int>(f.fallbacks.size()); ++i) {
        const std::string& family = f.fallbacks[static_cast<std::size_t>(i)];
        set_label(IDC_FALLBACK_TEXT + i, family.empty() ? L"None" : fbc::fonts::widen(family).c_str());
    }
}

bool ConfigureDialog::on_font_command(int id) {
    TabFont& f = state_.settings.font;
    const int slots = static_cast<int>(f.fallbacks.size());
    const std::string host = fbc::fonts::narrow(state_.host_font_family);
    if (id == IDC_FONT_PICK) {
        fbc::fonts::FontChoice choice = font_choice(f);
        if (!fbc::fonts::pick_font(m_hWnd, choice, {host, state_.host_font_tenths, 400})) return true;
        f.family = choice.family;
        f.tenths_pt = static_cast<std::uint16_t>((std::min)(choice.tenths_pt, 720u));
        f.weight = static_cast<std::uint16_t>((std::min)(choice.weight, 1000u));
        f.italic = choice.italic;
    } else if (id == IDC_FONT_CLEAR) {
        f.family.clear();
        f.tenths_pt = 0;
        f.weight = 0;
        f.italic = false;
    } else if (id >= IDC_FALLBACK_PICK && id < IDC_FALLBACK_PICK + slots) {
        std::string& slot = f.fallbacks[static_cast<std::size_t>(id - IDC_FALLBACK_PICK)];
        if (!fbc::fonts::pick_family(m_hWnd, slot, f.family.empty() ? host : f.family)) return true;
    } else if (id >= IDC_FALLBACK_CLEAR && id < IDC_FALLBACK_CLEAR + slots) {
        f.fallbacks[static_cast<std::size_t>(id - IDC_FALLBACK_CLEAR)].clear();
    } else {
        return false;
    }
    show_fonts();
    changed();
    return true;
}

void ConfigureDialog::update_enabled() {
    const Settings& s = state_.settings;
    enable(IDC_ROTATE, s.position == StripPosition::left || s.position == StripPosition::right);
    enable(IDC_ALIGN, s.sizing != TabSizing::fill);
    // The strength is the opacity of the accent fill, which only the pill and the tab have.
    const bool fill = s.indicator == Indicator::pill || s.indicator == Indicator::tab;
    enable(IDC_STRENGTH_AUTO, fill);
    const bool line = s.indicator == Indicator::underline || s.indicator == Indicator::tab_outline;
    enable(IDC_LINE_WIDTH_AUTO, line);
    enable(IDC_LINE_WIDTH, line && s.line_width != 0);
    enable(IDC_STRENGTH, fill && s.accent_strength != 0);
    enable(IDC_STRENGTH_VALUE, fill && s.accent_strength != 0);
    enable(IDC_CHIP_COLOUR, s.chip);
    const bool custom_chip = s.chip && s.chip_colour == ChipColour::custom;
    enable(IDC_CHIP_HEX, custom_chip);
    enable(IDC_CHIP_SWATCH, custom_chip);
    enable(IDC_CHIP_STRENGTH_AUTO, s.chip);
    enable(IDC_CHIP_STRENGTH, s.chip && s.chip_strength != 0);
    enable(IDC_CHIP_STRENGTH_VALUE, s.chip && s.chip_strength != 0);
    const bool custom_accent = s.accent_source == AccentSource::custom;
    enable(IDC_ACCENT_HEX, custom_accent);
    enable(IDC_ACCENT_SWATCH, custom_accent);
    const bool custom_background = s.strip_background == StripBackground::custom;
    enable(IDC_BACKGROUND_HEX, custom_background);
    enable(IDC_BACKGROUND_SWATCH, custom_background);
    const bool tint = s.strip_background == StripBackground::accent_tint;
    enable(IDC_TINT, tint);
    enable(IDC_TINT_VALUE, tint);
    enable(IDC_TRANSPARENT_OPACITY, s.transparent_background);
    enable(IDC_TRANSPARENT_OPACITY_VALUE, s.transparent_background);
    enable(IDC_TEXT_HEX, s.custom_text);
    enable(IDC_TEXT_SWATCH, s.custom_text);
    enable(IDC_ACTIVE_TEXT_HEX, s.custom_active_text);
    enable(IDC_ACTIVE_TEXT_SWATCH, s.custom_active_text);

    for (const HoverSet& set : hover_sets) hover_enabled(set);
    // The fade is shared: it matters while either set shows something.
    const bool fades = s.hover_style != HoverStyle::none || s.hover_text != HoverText::unchanged ||
                       s.active_hover_style != HoverStyle::none || s.active_hover_text != HoverText::unchanged;
    enable(IDC_HOVER_FADE, fades);
    enable(IDC_HOVER_FADE_MS, s.hover_fade && fades);

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

void ConfigureDialog::finish(bool ok) {
    if (!modeless_) {
        EndDialog(ok ? IDOK : IDCANCEL);
        return;
    }
    told_ = true;
    target_.configure_closed(ok, state_);
    DestroyWindow();
}

void ConfigureDialog::on_destroy() {
    SetMsgHandled(FALSE);
    if (icon_font_ != nullptr) ::DeleteObject(icon_font_);
    if (emoji_font_ != nullptr) ::DeleteObject(emoji_font_);
    icon_font_ = emoji_font_ = nullptr;
    if (!modeless_) return;
    modeless_dialog_manager::g_remove(m_hWnd);
    // Destroyed with its owner (foobar2000 closing): as Cancel.
    if (!told_) {
        told_ = true;
        target_.configure_closed(false, state_);
    }
}

LRESULT ConfigureDialog::on_close_quietly(UINT, WPARAM, LPARAM) {
    told_ = true;
    DestroyWindow();
    return 0;
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
        finish(true);
        return;
    case IDCANCEL: finish(false); return;
    default: break;
    }
    if (loading_) return;
    if (code == BN_CLICKED && on_font_command(id)) return;
    switch (id) {
    case IDC_DEFAULTS:
        state_.settings = Settings{};
        settings_to_controls();
        changed();
        return;
    case IDC_ACCENT_SWATCH:
    case IDC_BACKGROUND_SWATCH:
    case IDC_HOVER_SWATCH:
    case IDC_HOVER_TEXT_SWATCH:
    case IDC_HOVER_ACTIVE_SWATCH:
    case IDC_HOVER_ACTIVE_TEXT_SWATCH:
    case IDC_TEXT_SWATCH:
    case IDC_ACTIVE_TEXT_SWATCH:
    case IDC_CHIP_SWATCH: {
        if (code != BN_CLICKED) return;
        const Swatch& swatch = swatch_for(id);
        const int hex = swatch.hex;
        std::uint32_t argb = state_.settings.*swatch.field;
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
    if (code == BN_CLICKED && checked(id)) {
        // Ticking Automatic puts the slider (or the width) back where it rests: on the automatic
        // value.
        int bar = 0, rest = 0, width = 0;
        switch (id) {
        case IDC_STRENGTH_AUTO: bar = IDC_STRENGTH, rest = state_.dark ? auto_fill_dark : auto_fill_light; break;
        case IDC_CHIP_STRENGTH_AUTO:
            bar = IDC_CHIP_STRENGTH, rest = state_.dark ? auto_chip_dark : auto_chip_light;
            break;
        case IDC_HOVER_FILL_AUTO: bar = IDC_HOVER_FILL, rest = auto_hover_fill; break;
        case IDC_HOVER_ACTIVE_FILL_AUTO: bar = IDC_HOVER_ACTIVE_FILL, rest = auto_active_hover_fill; break;
        case IDC_HOVER_LINE_AUTO: bar = IDC_HOVER_LINE, rest = 50; break;
        case IDC_HOVER_ACTIVE_LINE_AUTO: bar = IDC_HOVER_ACTIVE_LINE, rest = 50; break;
        case IDC_LINE_WIDTH_AUTO: width = IDC_LINE_WIDTH, rest = auto_line_width(false); break;
        case IDC_HOVER_LINE_WIDTH_AUTO:
            width = IDC_HOVER_LINE_WIDTH, rest = auto_line_width(hover_outline(state_.settings.hover_style));
            break;
        case IDC_HOVER_ACTIVE_LINE_WIDTH_AUTO:
            width = IDC_HOVER_ACTIVE_LINE_WIDTH;
            rest = auto_line_width(hover_outline(state_.settings.active_hover_style));
            break;
        default: break;
        }
        if (bar != 0) ::SendMessageW(control(bar), TBM_SETPOS, TRUE, rest);
        if (width != 0) {
            loading_ = true;
            set_number(width, static_cast<std::uint16_t>(rest));
            loading_ = false;
        }
    }
    if (code == EN_CHANGE || code == BN_CLICKED || code == CBN_SELCHANGE) {
        settings_from_controls();
        for (const Swatch& swatch : swatches) {
            if (id == swatch.hex) ::InvalidateRect(control(swatch.button), nullptr, FALSE);
        }
        changed();
    }
}

LRESULT ConfigureDialog::on_hscroll(UINT, WPARAM, LPARAM lparam) {
    const auto bar = reinterpret_cast<HWND>(lparam);
    const auto ours = [this, bar] {
        for (const int id : {IDC_STRENGTH, IDC_CHIP_STRENGTH, IDC_TINT, IDC_TRANSPARENT_OPACITY, IDC_HOVER_FILL,
                             IDC_HOVER_LINE, IDC_HOVER_ACTIVE_FILL, IDC_HOVER_ACTIVE_LINE}) {
            if (bar == control(id)) return true;
        }
        return false;
    };
    if (loading_ || bar == nullptr || !ours()) {
        SetMsgHandled(FALSE);
        return 0;
    }
    settings_from_controls();
    changed();
    return 0;
}

void ConfigureDialog::on_draw_item(UINT, LPDRAWITEMSTRUCT item) {
    const auto is_swatch = [](UINT id) {
        for (const Swatch& swatch : swatches) {
            if (static_cast<UINT>(swatch.button) == id) return true;
        }
        return false;
    };
    if (item == nullptr || !is_swatch(item->CtlID)) {
        SetMsgHandled(FALSE);
        return;
    }
    const Swatch& swatch = swatch_for(static_cast<int>(item->CtlID));
    const std::uint32_t argb = parse_rgb(window_text(control(swatch.hex)), state_.settings.*swatch.field);
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

HWND open_configure_dialog(HWND owner, const ConfigureState& state, ConfigureTarget& target) {
    auto* dialog = new ConfigureDialog(state, target);
    if (dialog->Create(owner) == nullptr) {
        delete dialog;
        return nullptr;
    }
    const HWND wnd = dialog->m_hWnd;
    // Tab, Enter and Esc work as in a modal dialog.
    modeless_dialog_manager::g_add(wnd);
    ::ShowWindow(wnd, SW_SHOW);
    return wnd;
}

void close_configure_dialog(HWND wnd) {
    if (wnd != nullptr && ::IsWindow(wnd) != FALSE) ::SendMessageW(wnd, ConfigureDialog::wm_close_quietly, 0, 0);
}

bool run_rename_dialog(HWND parent, const std::wstring& panel_name, TabExtra& extra) {
    RenameDialog dialog(panel_name, extra);
    return dialog.DoModal(parent) == IDOK;
}

} // namespace bettertabs
