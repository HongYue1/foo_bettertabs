#include <windows.h>
#include <windowsx.h>

#include <commctrl.h>
#include <uxtheme.h>

#include "strip_window.h"

#include <dwrite_2.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

#include <algorithm>
#include <cmath>
#include <string>

#include "../model/colour.h"
#include "../platform/graphics.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace bettertabs {

namespace {

constexpr wchar_t class_name[] = L"foo_bettertabs_strip";
constexpr UINT wm_dpichanged_afterparent = 0x02E3;
constexpr float wide_layout = 100000.0f;
constexpr UINT_PTR switch_timer = 0xB710;

[[nodiscard]] HINSTANCE module_instance() noexcept {
    return reinterpret_cast<HINSTANCE>(&__ImageBase);
}

//! Icons from the Private Use Area: the newest installed icon font of the system's own UI
//! (Windows 11, Windows 10, then Windows 7/8). Looked up once.
[[nodiscard]] const std::wstring& system_icon_family() noexcept {
    static const std::wstring family = [] {
        std::wstring found = L"Segoe UI Symbol";
        IDWriteFactory* factory = gfx::dwrite();
        if (factory == nullptr) return found;
        com_ptr<IDWriteFontCollection> fonts;
        if (FAILED(factory->GetSystemFontCollection(fonts.put(), FALSE))) return found;
        for (const wchar_t* name : {L"Segoe Fluent Icons", L"Segoe MDL2 Assets", L"Segoe UI Symbol"}) {
            UINT32 index = 0;
            BOOL exists = FALSE;
            if (SUCCEEDED(fonts->FindFamilyName(name, &index, &exists)) && exists) {
                found = name;
                break;
            }
        }
        return found;
    }();
    return family;
}

[[nodiscard]] bool private_use(const std::wstring& glyph) noexcept {
    return !glyph.empty() && glyph[0] >= 0xE000 && glyph[0] <= 0xF8FF;
}

//! Icon fonts are drawn on a 1 em square; this keeps them optically level with the label.
constexpr float icon_scale = 1.25f;

[[nodiscard]] D2D1_COLOR_F d2d_colour(COLORREF c, float alpha = 1.0f) noexcept {
    return D2D1_COLOR_F{static_cast<float>(GetRValue(c)) / 255.0f, static_cast<float>(GetGValue(c)) / 255.0f,
                        static_cast<float>(GetBValue(c)) / 255.0f, alpha};
}

//! t of `a` over (1 - t) of `b`, opaque. Inactive text is a blend, never real alpha, so ClearType
//! stays available.
[[nodiscard]] COLORREF blend(COLORREF a, COLORREF b, float t) noexcept {
    const auto mix = [t](int x, int y) {
        return static_cast<BYTE>(std::lround(static_cast<float>(x) * t + static_cast<float>(y) * (1.0f - t)));
    };
    return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
}

[[nodiscard]] bool intersects(const RECT& a, const RECT& b) noexcept {
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

[[nodiscard]] int ceil_px(float value) noexcept { return static_cast<int>(std::ceil(value - 0.001f)); }

enum class Edge : std::uint8_t { top, bottom, left, right };

//! The active tab's fill colour at `alpha`: the line accent for a faint wash (it carries the hue
//! best when mostly the strip shows through), the fill accent from about 90% on, and an OKLCh
//! blend between.
[[nodiscard]] COLORREF accent_fill_colour(const StripTheme& theme, float alpha) noexcept {
    if (theme.fill_accent == CLR_INVALID || theme.fill_accent == theme.accent) return theme.accent;
    const float x = std::clamp((alpha - 0.30f) / (0.90f - 0.30f), 0.0f, 1.0f);
    const float t = x * x * (3.0f - 2.0f * x);
    if (t <= 0.0f) return theme.accent;
    if (t >= 1.0f) return theme.fill_accent;
    const colour::Lab a = colour::from_rgb(colour::rgb_from_colorref(theme.accent));
    const colour::Lab b = colour::from_rgb(colour::rgb_from_colorref(theme.fill_accent));
    const float L = a.L + (b.L - a.L) * t;
    const float C = colour::chroma(a) + (colour::chroma(b) - colour::chroma(a)) * t;
    return colour::colorref_from_rgb(colour::from_lch(L, C, colour::hue(b)));
}

// The look. Overlay strengths are alpha of the text colour over the strip.
constexpr float hover_alpha_dark = 0.08f;
constexpr float hover_alpha_light = 0.06f;
constexpr float chip_alpha = 0.05f;
constexpr float chip_active_alpha = 0.18f;
constexpr float pill_alpha_dark = 0.30f;
constexpr float pill_alpha_light = 0.26f;
//! From this fill opacity on, the active tab's text is chosen for contrast against the fill.
constexpr float strong_fill = 0.40f;
constexpr float text_min_contrast = 4.5f;
constexpr float inactive_text = 0.70f;
//! Dark mode only: the strip is lifted off the panel so it reads as chrome, not content.
constexpr float dark_lift = 0.04f;

} // namespace

StripWindow::~StripWindow() {
    destroy();
    release_buffer();
}

bool StripWindow::create(HWND parent, StripListener& listener) noexcept {
    if (wnd_ != nullptr) return true;
    // QueryPerformanceCounter directly: the strip has no dependency on perf.cpp (render test).
    LARGE_INTEGER freq{};
    LARGE_INTEGER t{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t);
    const auto lap = [&](double& slot) {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        if (freq.QuadPart > 0) {
            slot = static_cast<double>(now.QuadPart - t.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart);
        }
        t = now;
    };
    create_timings_ = CreateTimings{};
    static const ATOM atom = [] {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &StripWindow::window_proc;
        wc.hInstance = module_instance();
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = class_name;
        return RegisterClassExW(&wc);
    }();
    if (atom == 0) return false;

    listener_ = &listener;
    cleartype_ = gfx::system_uses_cleartype();
    lap(create_timings_.class_ms);
    nccreate_qpc_ = 0;
    create_qpc_ = 0;
    const long long t_call = t.QuadPart;
    // No WM_PARENTNOTIFY up the whole ancestor chain (every Columns UI splitter up to the main
    // window): nothing needs it, and it is the one thing a create here sends outside the strip.
    const HWND wnd = CreateWindowExW(WS_EX_NOPARENTNOTIFY, class_name, L"", WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP, 0, 0, 0, 0, parent,
                                     nullptr, module_instance(), this);
    if (wnd == nullptr) {
        listener_ = nullptr;
        return false;
    }
    lap(create_timings_.window_ms);
    if (freq.QuadPart > 0 && nccreate_qpc_ != 0 && create_qpc_ != 0) {
        const auto ms = [&](long long a, long long b) {
            return static_cast<double>(b - a) * 1000.0 / static_cast<double>(freq.QuadPart);
        };
        create_timings_.to_nccreate_ms = ms(t_call, nccreate_qpc_);
        create_timings_.to_create_ms = ms(nccreate_qpc_, create_qpc_);
        create_timings_.after_create_ms = ms(create_qpc_, t.QuadPart);
    }
    dpi_ = dpi_override_ != 0 ? dpi_override_ : gfx::window_dpi(wnd);
    const LRESULT ui_state = SendMessageW(wnd, WM_QUERYUISTATE, 0, 0);
    hide_focus_ = (ui_state & UISF_HIDEFOCUS) != 0;
    lap(create_timings_.state_ms);
    rebuild_text_format();
    rebuild_items();
    update_thickness();
    lap(create_timings_.text_ms);
    // The tooltip is created on the first hover (on_mouse_move), not at startup.
    return true;
}

void StripWindow::destroy() noexcept {
    if (wnd_ != nullptr) DestroyWindow(wnd_);
    wnd_ = nullptr;
    target_.reset();
    brush_.reset();
}

// ---------------------------------------------------------------------------------------------
// Inputs from the host. Everything expensive happens here, never in WM_PAINT.

void StripWindow::set_settings(const Settings& settings) noexcept {
    if (settings == settings_) return;
    stop_switch();
    settings_ = settings;
    update_thickness();
    relayout();
    if (wnd_ != nullptr) InvalidateRect(wnd_, nullptr, FALSE);
}

void StripWindow::set_theme(const StripTheme& theme) noexcept {
    if (theme == theme_) return;
    if (tooltip_ != nullptr && theme.dark != theme_.dark) {
        SetWindowTheme(tooltip_, theme.dark ? L"DarkMode_Explorer" : nullptr, nullptr);
    }
    theme_ = theme;
    surface_ = theme.dark && theme.lift ? blend(theme.text, theme.background, dark_lift) : theme.background;
    if (wnd_ != nullptr) InvalidateRect(wnd_, nullptr, FALSE);
}

void StripWindow::set_text_options(const StripTextOptions& options) noexcept {
    const bool relayout_needed =
        options.gdi_compatible != text_options_.gdi_compatible || options.gdi_natural != text_options_.gdi_natural;
    text_options_ = options;
    draw_text_options_ = D2D1_DRAW_TEXT_OPTIONS_CLIP;
    if (options.colour_glyphs && gfx::colour_fonts_supported()) {
        draw_text_options_ = static_cast<D2D1_DRAW_TEXT_OPTIONS>(draw_text_options_ |
                                                                 D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    }
    if (target_) target_->SetTextRenderingParams(text_options_.params.get());
    if (relayout_needed) {
        // GDI-compatible layouts measure differently: the widths change.
        rebuild_text_format();
        rebuild_items();
        update_thickness();
        relayout();
    }
    if (wnd_ != nullptr) InvalidateRect(wnd_, nullptr, FALSE);
}

D2D1_TEXT_ANTIALIAS_MODE StripWindow::text_antialias() const noexcept {
    switch (text_options_.antialias) {
    case TextAntialias::greyscale: return D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE;
    case TextAntialias::aliased: return D2D1_TEXT_ANTIALIAS_MODE_ALIASED;
    case TextAntialias::automatic:
    default: return cleartype_ ? D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE : D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE;
    }
}

bool StripWindow::make_layout(IDWriteTextFormat* format, const std::wstring& text, float max_width,
                              float max_height, IDWriteTextLayout** out) const noexcept {
    IDWriteFactory* factory = gfx::dwrite();
    if (factory == nullptr || format == nullptr) return false;
    const auto length = static_cast<UINT32>(text.size());
    // The target works in device pixels at 96 "DPI", so one DIP of the layout is one pixel.
    const HRESULT hr = text_options_.gdi_compatible
                           ? factory->CreateGdiCompatibleTextLayout(text.c_str(), length, format, max_width,
                                                                    max_height, 1.0f, nullptr,
                                                                    text_options_.gdi_natural ? TRUE : FALSE, out)
                           : factory->CreateTextLayout(text.c_str(), length, format, max_width, max_height, out);
    return SUCCEEDED(hr);
}

void StripWindow::set_font(const StripFont& font) noexcept {
    stop_switch();
    font_ = font;
    font_set_ = true;
    rebuild_text_format();
    rebuild_items();
    update_thickness();
    relayout();
    if (wnd_ != nullptr) InvalidateRect(wnd_, nullptr, FALSE);
}

void StripWindow::set_items(std::span<const StripItem> items, std::size_t active) noexcept {
    if (dragging_) end_drag(false);
    stop_switch();
    try {
        if (items_.size() != items.size()) items_.resize(items.size());
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (items_[i].spec == items[i]) continue;
            items_[i].spec = items[i];
            items_[i].generation = 0; // rebuild this one
        }
    } catch (...) {
        items_.clear();
    }
    active_ = active < items_.size() ? active : no_index;
    hover_ = no_index;
    rebuild_items();
    update_thickness();
    relayout();
    update_tooltip();
    if (wnd_ != nullptr) InvalidateRect(wnd_, nullptr, FALSE);
}

void StripWindow::set_labels(std::span<const std::wstring> labels, std::size_t active) noexcept {
    try {
        std::vector<StripItem> items(labels.size());
        for (std::size_t i = 0; i < labels.size(); ++i) items[i].label = labels[i];
        set_items(items, active);
    } catch (...) {
    }
}

void StripWindow::set_active(std::size_t active) noexcept {
    if (active >= items_.size()) active = no_index;
    if (active == active_) return;
    const std::size_t old = active_;
    stop_switch();
    active_ = active;
    if (layout_.overflow && active != no_index && (active < layout_.first || active >= layout_.last)) {
        // The visible window of tabs has to move.
        relayout();
        if (wnd_ != nullptr) InvalidateRect(wnd_, nullptr, FALSE);
        return;
    }
    invalidate_tab(old);
    invalidate_tab(active);
    if (settings_.animations && wnd_ != nullptr && !dragging_ && old != no_index && active != no_index &&
        IsWindowVisible(wnd_) != FALSE) {
        const RECT a = tab_rect(old);
        const RECT b = tab_rect(active);
        if (IsRectEmpty(&a) == FALSE && IsRectEmpty(&b) == FALSE) start_switch(old);
    }
}

// ---------------------------------------------------------------------------------------------
// Tab switch animation.

void StripWindow::start_switch(std::size_t from) noexcept {
    switch_from_ = from;
    switch_start_ = perf::now();
    switch_t_ = 0.0f;
    switching_ = true;
    if (SetTimer(wnd_, switch_timer, USER_TIMER_MINIMUM, nullptr) == 0) {
        stop_switch();
        return;
    }
    const RECT r = switch_rect();
    InvalidateRect(wnd_, &r, FALSE);
}

void StripWindow::stop_switch() noexcept {
    if (!switching_) return;
    const RECT r = switch_rect();
    switching_ = false;
    switch_from_ = no_index;
    switch_t_ = 1.0f;
    if (wnd_ == nullptr) return;
    KillTimer(wnd_, switch_timer);
    if (IsRectEmpty(&r) == FALSE) InvalidateRect(wnd_, &r, FALSE);
}

void StripWindow::on_switch_timer() noexcept {
    if (!switching_) {
        KillTimer(wnd_, switch_timer);
        return;
    }
    const RECT r = switch_rect();
    if (IsRectEmpty(&r) != FALSE) {
        // A tab went out of view mid-way: just show the end state.
        stop_switch();
        InvalidateRect(wnd_, nullptr, FALSE);
        return;
    }
    const double length = static_cast<double>((std::max)(std::uint16_t{1}, settings_.switch_ms));
    const float p = static_cast<float>(std::clamp(perf::elapsed_ms(switch_start_, perf::now()) / length, 0.0, 1.0));
    const float rest = 1.0f - p;
    switch_t_ = 1.0f - rest * rest * rest; // ease-out cubic
    InvalidateRect(wnd_, &r, FALSE);
    if (p >= 1.0f) stop_switch();
}

RECT StripWindow::switch_rect() const noexcept {
    const RECT a = tab_rect(switch_from_);
    const RECT b = tab_rect(active_);
    if (IsRectEmpty(&a) != FALSE || IsRectEmpty(&b) != FALSE) return RECT{};
    RECT u{};
    UnionRect(&u, &a, &b);
    return u;
}

float StripWindow::active_fill_alpha() const noexcept {
    if (theme_.active_fill > 0.0f) return theme_.active_fill;
    if (settings_.indicator == Indicator::pill) return theme_.dark ? pill_alpha_dark : pill_alpha_light;
    return chip_active_alpha;
}

void StripWindow::draw_switch_indicator() noexcept {
    const RECT a = tab_rect(switch_from_);
    const RECT b = tab_rect(active_);
    if (IsRectEmpty(&a) != FALSE || IsRectEmpty(&b) != FALSE) return;
    const float t = switch_t_;
    const auto mix = [t](LONG from, LONG to) {
        return static_cast<float>(from) + (static_cast<float>(to) - static_cast<float>(from)) * t;
    };
    // Client pixels (render() has set the translation). The tab's long side runs along the strip.
    const D2D1_RECT_F r{mix(a.left, b.left), mix(a.top, b.top), mix(a.right, b.right), mix(a.bottom, b.bottom)};
    const bool along_x = horizontal();
    const float inset_along = static_cast<float>((std::max)(1, px(1)));
    const float inset_across = static_cast<float>((std::max)(2, px(3)));
    const float radius = static_cast<float>(px(settings_.corner_radius));

    if (settings_.indicator == Indicator::pill || settings_.chip) {
        D2D1_RECT_F bg = r;
        const float ix = along_x ? inset_along : inset_across;
        const float iy = along_x ? inset_across : inset_along;
        bg.left += ix;
        bg.right -= ix;
        bg.top += iy;
        bg.bottom -= iy;
        const float alpha = active_fill_alpha();
        brush_->SetColor(d2d_colour(accent_fill_colour(theme_, alpha), alpha));
        target_->FillRoundedRectangle(D2D1::RoundedRect(bg, radius, radius), brush_.get());
    }
    if (settings_.indicator == Indicator::underline) {
        const float bar = static_cast<float>((std::max)(2, px(2)));
        const float text_inset = static_cast<float>(px(settings_.pad_x)) * 0.5f;
        // Upright side tabs inset the bar like draw_tab does; rotated ones like a top strip.
        const float side_inset = rotated() ? text_inset : inset_along * 2.0f;
        D2D1_RECT_F u = r;
        switch (settings_.position) {
        case StripPosition::top: u = {r.left + text_inset, r.bottom - bar, r.right - text_inset, r.bottom}; break;
        case StripPosition::bottom: u = {r.left + text_inset, r.top, r.right - text_inset, r.top + bar}; break;
        case StripPosition::left: u = {r.right - bar, r.top + side_inset, r.right, r.bottom - side_inset}; break;
        case StripPosition::right: u = {r.left, r.top + side_inset, r.left + bar, r.bottom - side_inset}; break;
        }
        brush_->SetColor(d2d_colour(theme_.accent));
        target_->FillRoundedRectangle(D2D1::RoundedRect(u, bar / 2.0f, bar / 2.0f), brush_.get());
    }
}

void StripWindow::take_paint_stats(perf::PaintStats& out) noexcept {
    out = stats_;
    stats_ = perf::PaintStats{};
}

// ---------------------------------------------------------------------------------------------
// Text and metrics.

void StripWindow::rebuild_text_format() noexcept {
    text_format_.reset();
    icon_formats_.clear();
    ++generation_; // every item's layouts belong to the old format
    line_height_ = 0;
    if (!font_set_) return;
    IDWriteFactory* factory = gfx::dwrite();
    if (factory == nullptr) return;

    // Columns UI 3+: its own DirectWrite description (variable-font axes aside), plus its font
    // fallback for emoji. Sizes are DIPs; the target counts pixels.
    if (!font_.family.empty() && font_.size_dip > 0.0f) {
        try {
            com_ptr<IDWriteTextFormat> format;
            const float em = font_.size_dip * static_cast<float>(dpi_) / 96.0f;
            if (SUCCEEDED(factory->CreateTextFormat(font_.family.c_str(), nullptr, font_.weight, font_.style,
                                                    font_.stretch, em, L"", format.put()))) {
                text_format_ = std::move(format);
            }
        } catch (...) {
            text_format_.reset();
        }
    }

    LOGFONTW lf = font_.font;
    unsigned font_dpi = font_.font_dpi != 0 ? font_.font_dpi : 96;
    if (lf.lfFaceName[0] == L'\0') {
        NONCLIENTMETRICSW ncm{};
        ncm.cbSize = sizeof(ncm);
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
            lf = ncm.lfMessageFont;
            font_dpi = gfx::system_dpi();
        }
    }

    if (!text_format_) try {
        com_ptr<IDWriteGdiInterop> interop;
        if (FAILED(factory->GetGdiInterop(interop.put()))) return;
        com_ptr<IDWriteFont> font;
        if (FAILED(interop->CreateFontFromLOGFONT(&lf, font.put()))) {
            // An uninstalled face: fall back to the message font rather than to nothing.
            NONCLIENTMETRICSW ncm{};
            ncm.cbSize = sizeof(ncm);
            if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) return;
            lf = ncm.lfMessageFont;
            font_dpi = gfx::system_dpi();
            if (FAILED(interop->CreateFontFromLOGFONT(&lf, font.put()))) return;
        }

        std::wstring family_name = L"Segoe UI";
        com_ptr<IDWriteFontFamily> family;
        com_ptr<IDWriteLocalizedStrings> names;
        if (SUCCEEDED(font->GetFontFamily(family.put())) && SUCCEEDED(family->GetFamilyNames(names.put()))) {
            UINT32 index = 0;
            BOOL exists = FALSE;
            if (FAILED(names->FindLocaleName(L"en-us", &index, &exists)) || !exists) index = 0;
            UINT32 length = 0;
            if (SUCCEEDED(names->GetStringLength(index, &length))) {
                std::wstring name(length + 1, L'\0');
                if (SUCCEEDED(names->GetString(index, name.data(), length + 1))) {
                    name.resize(length);
                    family_name = std::move(name);
                }
            }
        }

        DWRITE_FONT_METRICS metrics{};
        font->GetMetrics(&metrics);
        float em = 12.0f;
        if (lf.lfHeight < 0) {
            em = static_cast<float>(-lf.lfHeight);
        } else if (lf.lfHeight > 0 && metrics.ascent + metrics.descent != 0) {
            // Positive lfHeight is the cell height, not the em.
            em = static_cast<float>(lf.lfHeight) * static_cast<float>(metrics.designUnitsPerEm) /
                 static_cast<float>(metrics.ascent + metrics.descent);
        } else {
            font_dpi = 96;
        }
        em = em * static_cast<float>(dpi_) / static_cast<float>(font_dpi);

        com_ptr<IDWriteTextFormat> format;
        if (FAILED(factory->CreateTextFormat(family_name.c_str(), nullptr, font->GetWeight(), font->GetStyle(),
                                             font->GetStretch(), em, L"", format.put()))) {
            return;
        }
        text_format_ = std::move(format);
    } catch (...) {
        text_format_.reset();
    }
    if (!text_format_) return;

    try {
        IDWriteTextFormat* format = text_format_.get();
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        com_ptr<IDWriteInlineObject> ellipsis;
        if (SUCCEEDED(factory->CreateEllipsisTrimmingSign(format, ellipsis.put()))) {
            format->SetTrimming(&trimming, ellipsis.get());
        }
        if (font_.fallback) {
            // IDWriteTextFormat1 is Windows 8.1+; earlier, the system fallback applies.
            com_ptr<IDWriteTextFormat1> format1;
            com_ptr<IDWriteFontFallback> fallback;
            if (SUCCEEDED(format->QueryInterface(__uuidof(IDWriteTextFormat1), reinterpret_cast<void**>(format1.put()))) &&
                SUCCEEDED(font_.fallback->QueryInterface(__uuidof(IDWriteFontFallback),
                                                         reinterpret_cast<void**>(fallback.put())))) {
                format1->SetFontFallback(fallback.get());
            }
        }

        com_ptr<IDWriteTextLayout> probe;
        if (make_layout(format, L"Ag", wide_layout, wide_layout, probe.put())) {
            DWRITE_TEXT_METRICS tm{};
            if (SUCCEEDED(probe->GetMetrics(&tm))) line_height_ = ceil_px(tm.height);
        }
        if (line_height_ <= 0) line_height_ = ceil_px(format->GetFontSize() * 1.33f);
    } catch (...) {
        text_format_.reset();
    }
}

void StripWindow::rebuild_items() noexcept {
    for (Item& item : items_) {
        if (item.generation != generation_) build_item(item);
    }
}

void StripWindow::build_item(Item& item) noexcept {
    item.layout.reset();
    item.icon_layout.reset();
    item.text_width = 0;
    item.text_height = line_height_;
    item.draw_width = 0;
    item.icon_width = 0;
    item.icon_height = 0;
    item.icon_gap = 0;
    item.generation = generation_;
    if (!text_format_) return;
    if (!item.spec.label.empty() &&
        make_layout(text_format_.get(), item.spec.label, wide_layout, static_cast<float>(line_height_),
                    item.layout.put())) {
        DWRITE_TEXT_METRICS tm{};
        if (SUCCEEDED(item.layout->GetMetrics(&tm))) {
            item.text_width = ceil_px(tm.widthIncludingTrailingWhitespace);
            item.text_height = (std::max)(line_height_, ceil_px(tm.height));
        }
    }
    item.draw_width = item.text_width;
    if (!item.spec.icon.empty()) {
        IDWriteTextFormat* format =
            private_use(item.spec.icon) ? icon_format(item.spec.icon_font) : text_format_.get();
        if (format != nullptr && make_layout(format, item.spec.icon, wide_layout, wide_layout, item.icon_layout.put())) {
            DWRITE_TEXT_METRICS tm{};
            if (SUCCEEDED(item.icon_layout->GetMetrics(&tm))) {
                item.icon_width = ceil_px(tm.widthIncludingTrailingWhitespace);
                item.icon_height = ceil_px(tm.height);
            }
        }
        if (item.icon_width <= 0) item.icon_layout.reset();
    }
    if (item.icon_width > 0 && item.text_width > 0) item.icon_gap = px(6);
}

IDWriteTextFormat* StripWindow::icon_format(const std::wstring& family) noexcept {
    for (auto& [name, format] : icon_formats_) {
        if (name == family) return format.get();
    }
    IDWriteFactory* factory = gfx::dwrite();
    if (factory == nullptr || !text_format_) return nullptr;
    try {
        const std::wstring& face = family.empty() ? system_icon_family() : family;
        com_ptr<IDWriteTextFormat> format;
        if (FAILED(factory->CreateTextFormat(face.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                             DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                             std::round(text_format_->GetFontSize() * icon_scale), L"",
                                             format.put()))) {
            return nullptr;
        }
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        IDWriteTextFormat* raw = format.get();
        icon_formats_.emplace_back(family, std::move(format));
        return raw;
    } catch (...) {
        return nullptr;
    }
}

void StripWindow::update_thickness() noexcept {
    const int pad_x = px(settings_.pad_x);
    const int pad_y = px(settings_.pad_y);
    int value = 0;
    if (horizontal() || rotated()) {
        int tallest = line_height_;
        for (const Item& item : items_) tallest = (std::max)(tallest, item.icon_height);
        value = tallest + 2 * pad_y;
    } else {
        int widest = 0;
        for (const Item& item : items_) widest = (std::max)(widest, capped_width(item));
        value = std::clamp(widest + 2 * pad_x, px(48), px(280));
    }
    value = (std::max)(value, px(settings_.thickness));
    thickness_ = (std::max)(value, 1);
}

void StripWindow::relayout() noexcept {
    const int pad_x = px(settings_.pad_x);
    const int pad_y = px(settings_.pad_y);
    const bool along_text = horizontal() || rotated();
    try {
        extents_.resize(items_.size());
        for (std::size_t i = 0; i < items_.size(); ++i) {
            extents_[i] = along_text ? capped_width(items_[i]) + 2 * pad_x
                                     : (std::max)(line_height_, items_[i].icon_height) + 2 * pad_y;
        }
        StripLayoutInput in;
        in.length = horizontal() ? width_ : height_;
        in.sizing = settings_.sizing;
        in.align = settings_.align;
        in.chevron_position = settings_.chevron_position;
        in.spacing = px(settings_.spacing);
        in.chevron = px(24);
        in.extents = extents_;
        in.active = active_;
        // Along the text a tab can give up all but a few characters before the chevron appears.
        if (along_text && settings_.shrink_titles) in.shrink_floor = 3 * line_height_ + 2 * pad_x;
        layout_strip(in, layout_);
    } catch (...) {
        layout_ = StripLayout{};
        return;
    }

    // Clipped tabs get an ellipsis: narrow their layouts now so painting only reads them.
    const int across_room = (horizontal() ? height_ : width_) - 2 * pad_x;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        Item& item = items_[i];
        if (!item.layout) continue;
        int room = settings_.max_tab_width != 0 ? px(settings_.max_tab_width) : item.text_width;
        if (i < layout_.tabs.size() && layout_.tabs[i].length > 0) {
            room = along_text ? layout_.tabs[i].length - 2 * pad_x : across_room;
            room -= item.icon_width + item.icon_gap;
        }
        if (settings_.max_tab_width != 0) room = (std::min)(room, px(settings_.max_tab_width));
        room = (std::max)(0, room);
        const int draw = (std::min)(item.text_width, room);
        if (draw != item.draw_width) {
            item.layout->SetMaxWidth(draw < item.text_width ? static_cast<float>(draw) : wide_layout);
            item.draw_width = draw;
        }
    }
}

int StripWindow::capped_width(const Item& item) const noexcept {
    int text = item.text_width;
    if (settings_.max_tab_width != 0) text = (std::min)(text, px(settings_.max_tab_width));
    return item.icon_width + item.icon_gap + text;
}

int StripWindow::px(int dips) const noexcept { return MulDiv(dips, static_cast<int>(dpi_), 96); }

bool StripWindow::horizontal() const noexcept {
    return settings_.position == StripPosition::top || settings_.position == StripPosition::bottom;
}

bool StripWindow::rotated() const noexcept { return !horizontal() && settings_.side_text == SideText::rotated; }

RECT StripWindow::tab_rect(std::size_t index) const noexcept {
    if (index >= layout_.tabs.size() || layout_.tabs[index].length <= 0) return RECT{};
    const Span& s = layout_.tabs[index];
    return horizontal() ? RECT{s.start, 0, s.end(), height_} : RECT{0, s.start, width_, s.end()};
}

RECT StripWindow::chevron_rect() const noexcept {
    if (!layout_.overflow || layout_.chevron.length <= 0) return RECT{};
    const Span& s = layout_.chevron;
    return horizontal() ? RECT{s.start, 0, s.end(), height_} : RECT{0, s.start, width_, s.end()};
}

std::size_t StripWindow::hit_test(POINT pt) const noexcept {
    if (pt.x < 0 || pt.y < 0 || pt.x >= width_ || pt.y >= height_) return no_index;
    return hit_test_strip(layout_, horizontal() ? pt.x : pt.y);
}

bool StripWindow::chevron_hit(POINT pt) const noexcept {
    const RECT r = chevron_rect();
    return PtInRect(&r, pt) != FALSE;
}

void StripWindow::invalidate_tab(std::size_t index) noexcept {
    if (wnd_ == nullptr || index == no_index) return;
    const RECT r = tab_rect(index);
    if (r.right > r.left && r.bottom > r.top) InvalidateRect(wnd_, &r, FALSE);
}

// ---------------------------------------------------------------------------------------------
// Back buffer and rendering.

bool StripWindow::ensure_buffer(int width, int height) noexcept {
    if (width <= 0 || height <= 0) return false;
    if (dib_ != nullptr && width <= buffer_width_ && height <= buffer_height_) return true;
    // Grow in steps so dragging a splitter does not reallocate on every pixel.
    const int new_width = (std::max)(buffer_width_, (width + 255) / 256 * 256);
    const int new_height = (std::max)(buffer_height_, (height + 31) / 32 * 32);
    release_buffer();
    mem_dc_ = CreateCompatibleDC(nullptr);
    if (mem_dc_ == nullptr) return false;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = new_width;
    bi.bmiHeader.biHeight = -new_height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    dib_ = CreateDIBSection(mem_dc_, &bi, DIB_RGB_COLORS, &bits_, nullptr, 0);
    if (dib_ == nullptr) {
        release_buffer();
        return false;
    }
    old_bitmap_ = SelectObject(mem_dc_, dib_);
    buffer_width_ = new_width;
    buffer_height_ = new_height;
    return true;
}

void StripWindow::release_buffer() noexcept {
    if (mem_dc_ != nullptr && old_bitmap_ != nullptr) SelectObject(mem_dc_, old_bitmap_);
    if (dib_ != nullptr) DeleteObject(dib_);
    if (mem_dc_ != nullptr) DeleteDC(mem_dc_);
    mem_dc_ = nullptr;
    dib_ = nullptr;
    old_bitmap_ = nullptr;
    bits_ = nullptr;
    buffer_width_ = 0;
    buffer_height_ = 0;
}

bool StripWindow::ensure_target() noexcept {
    if (target_) return true;
    ID2D1Factory* factory = gfx::d2d();
    if (factory == nullptr) return false;
    // Software: the strip is small, and a hardware DC target would read back from the GPU on
    // every paint.
    const D2D1_RENDER_TARGET_PROPERTIES props{D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                              D2D1_PIXEL_FORMAT{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE},
                                              96.0f, 96.0f, D2D1_RENDER_TARGET_USAGE_NONE,
                                              D2D1_FEATURE_LEVEL_DEFAULT};
    if (FAILED(factory->CreateDCRenderTarget(&props, target_.put()))) return false;
    if (FAILED(target_->CreateSolidColorBrush(d2d_colour(theme_.text), brush_.put()))) {
        target_.reset();
        return false;
    }
    if (text_options_.params) target_->SetTextRenderingParams(text_options_.params.get());
    return true;
}

bool StripWindow::render(const RECT& dirty_in) noexcept {
    if (!ensure_buffer(width_, height_) || !ensure_target()) return false;
    RECT dirty{(std::max)(0L, dirty_in.left), (std::max)(0L, dirty_in.top), (std::min)(dirty_in.right, LONG{width_}),
               (std::min)(dirty_in.bottom, LONG{height_})};
    if (dirty.right <= dirty.left || dirty.bottom <= dirty.top) return true;

    if (FAILED(target_->BindDC(mem_dc_, &dirty))) return false;
    origin_x_ = static_cast<float>(dirty.left);
    origin_y_ = static_cast<float>(dirty.top);
    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Translation(-origin_x_, -origin_y_));
    target_->SetTextAntialiasMode(text_antialias());
    target_->Clear(d2d_colour(surface_));
    if (switching_) draw_switch_indicator();

    for (std::size_t i = layout_.first; i < layout_.last && i < items_.size(); ++i) {
        const RECT r = tab_rect(i);
        if (intersects(r, dirty)) draw_tab(i);
    }
    if (layout_.overflow) {
        const RECT r = chevron_rect();
        if (intersects(r, dirty)) draw_chevron();
    }

    const HRESULT hr = target_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        brush_.reset();
        target_.reset();
        return false;
    }
    return SUCCEEDED(hr);
}

void StripWindow::draw_tab(std::size_t index) noexcept {
    const Item& item = items_[index];
    const RECT r = tab_rect(index);
    const bool active = index == active_;
    const bool hover = index == hover_;
    // While switching, the moving indicator carries the active fill and underline
    // (draw_switch_indicator). Text and icon colours change at once, not with the slide: a fade
    // between, say, white and black text passes through an unreadable grey.
    const bool active_look = active && !switching_;
    const D2D1::Matrix3x2F base = D2D1::Matrix3x2F::Translation(-origin_x_, -origin_y_);

    // Work in a "frame": the tab as a horizontal rectangle, plus the edge facing the panel.
    D2D1_RECT_F f{static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(r.right),
                  static_cast<float>(r.bottom)};
    Edge edge = Edge::bottom;
    switch (settings_.position) {
    case StripPosition::top: edge = Edge::bottom; break;
    case StripPosition::bottom: edge = Edge::top; break;
    case StripPosition::left: edge = Edge::right; break;
    case StripPosition::right: edge = Edge::left; break;
    }
    const bool turn = rotated();
    if (turn) {
        const float cx = (f.left + f.right) / 2.0f;
        const float cy = (f.top + f.bottom) / 2.0f;
        const float len = f.bottom - f.top;
        const float across = f.right - f.left;
        // Left strip reads bottom to top, right strip top to bottom; either way the frame's
        // bottom edge ends up facing the panel.
        const float angle = settings_.position == StripPosition::left ? -90.0f : 90.0f;
        target_->SetTransform(D2D1::Matrix3x2F::Rotation(angle, D2D1::Point2F(cx, cy)) * base);
        f = D2D1_RECT_F{cx - len / 2.0f, cy - across / 2.0f, cx + len / 2.0f, cy + across / 2.0f};
        edge = Edge::bottom;
    }
    const bool frame_horizontal = edge == Edge::top || edge == Edge::bottom;

    const float inset_along = static_cast<float>((std::max)(1, px(1)));
    const float inset_across = static_cast<float>((std::max)(2, px(3)));
    const float radius = static_cast<float>(px(settings_.corner_radius));
    D2D1_RECT_F bg = f;
    if (frame_horizontal) {
        bg.left += inset_along;
        bg.right -= inset_along;
        bg.top += inset_across;
        bg.bottom -= inset_across;
    } else {
        bg.left += inset_across;
        bg.right -= inset_across;
        bg.top += inset_along;
        bg.bottom -= inset_along;
    }

    // One fill at most, strongest first; every one is a blend over the opaque strip, so text
    // drawn on top keeps ClearType.
    float fill_alpha = 0.0f;
    COLORREF fill = theme_.text;
    const bool accent_fill = active_look && (settings_.indicator == Indicator::pill || settings_.chip);
    if (accent_fill) {
        fill = theme_.accent;
        fill_alpha = active_fill_alpha();
    } else if (hover) {
        fill_alpha = (theme_.dark ? hover_alpha_dark : hover_alpha_light) + (settings_.chip ? chip_alpha : 0.0f);
    } else if (settings_.chip) {
        fill_alpha = chip_alpha;
    }
    if (accent_fill) fill = accent_fill_colour(theme_, fill_alpha);
    if (fill_alpha > 0.0f) {
        brush_->SetColor(d2d_colour(fill, fill_alpha));
        target_->FillRoundedRectangle(D2D1::RoundedRect(bg, radius, radius), brush_.get());
    }

    const int pad_x = px(settings_.pad_x);
    if (active_look && settings_.indicator == Indicator::underline) {
        const float bar = static_cast<float>((std::max)(2, px(2)));
        D2D1_RECT_F u = f;
        const float along_inset = static_cast<float>(pad_x) * 0.5f;
        switch (edge) {
        case Edge::bottom: u = {f.left + along_inset, f.bottom - bar, f.right - along_inset, f.bottom}; break;
        case Edge::top: u = {f.left + along_inset, f.top, f.right - along_inset, f.top + bar}; break;
        case Edge::right: u = {f.right - bar, f.top + inset_along * 2, f.right, f.bottom - inset_along * 2}; break;
        case Edge::left: u = {f.left, f.top + inset_along * 2, f.left + bar, f.bottom - inset_along * 2}; break;
        }
        brush_->SetColor(d2d_colour(theme_.accent));
        target_->FillRoundedRectangle(D2D1::RoundedRect(u, bar / 2.0f, bar / 2.0f), brush_.get());
    }

    if (item.layout || item.icon_layout) {
        const int content = item.icon_width + item.icon_gap + item.draw_width;
        float x = f.left + static_cast<float>(pad_x);
        if (frame_horizontal) {
            const float room = f.right - f.left;
            x = f.left + std::floor((std::max)(static_cast<float>(pad_x), (room - static_cast<float>(content)) / 2.0f));
        }
        const float y = f.top + std::floor((f.bottom - f.top - static_cast<float>(item.text_height)) / 2.0f);
        COLORREF text = active || hover ? theme_.text : blend(theme_.text, surface_, inactive_text);
        const bool final_fill = active && (settings_.indicator == Indicator::pill || settings_.chip);
        const float final_alpha = final_fill ? active_fill_alpha() : 0.0f;
        if (final_fill && final_alpha >= strong_fill) {
            // A strong accent fill: keep the theme's text if it still reads, else white or black.
            // Judged against the fill the tab ends with, also while it is still sliding in.
            const COLORREF final_colour = accent_fill_colour(theme_, final_alpha);
            const std::uint32_t under = colour::rgb_from_colorref(blend(final_colour, surface_, final_alpha));
            const std::uint32_t own = colour::rgb_from_colorref(text);
            if (colour::contrast_ratio(own, under) < text_min_contrast) {
                text = colour::contrast_ratio(0xFFFFFFu, under) >= colour::contrast_ratio(0x000000u, under)
                           ? RGB(255, 255, 255)
                           : RGB(0, 0, 0);
            }
        }
        target_->PushAxisAlignedClip(f, D2D1_ANTIALIAS_MODE_ALIASED);
        if (item.icon_layout) {
            // The active tab's icon carries the accent unless a fill already does.
            const bool accent_icon = active && settings_.indicator == Indicator::underline && !settings_.chip;
            const COLORREF icon = accent_icon ? theme_.accent : text;
            const float iy = f.top + std::floor((f.bottom - f.top - static_cast<float>(item.icon_height)) / 2.0f);
            brush_->SetColor(d2d_colour(icon));
            target_->DrawTextLayout(D2D1::Point2F(x, iy), item.icon_layout.get(), brush_.get(), draw_text_options_);
        }
        if (item.layout) {
            brush_->SetColor(d2d_colour(text));
            target_->DrawTextLayout(D2D1::Point2F(x + static_cast<float>(item.icon_width + item.icon_gap), y),
                                    item.layout.get(), brush_.get(), draw_text_options_);
        }
        target_->PopAxisAlignedClip();
    }

    if (active && focused_ && !hide_focus_) {
        D2D1_RECT_F focus = bg;
        focus.left += 0.5f;
        focus.top += 0.5f;
        focus.right -= 0.5f;
        focus.bottom -= 0.5f;
        brush_->SetColor(d2d_colour(theme_.text, 0.6f));
        target_->DrawRoundedRectangle(D2D1::RoundedRect(focus, radius, radius), brush_.get(), 1.0f);
    }

    if (turn) target_->SetTransform(base);
}

void StripWindow::draw_chevron() noexcept {
    const RECT r = chevron_rect();
    const float cx = static_cast<float>(r.left + r.right) / 2.0f;
    const float cy = static_cast<float>(r.top + r.bottom) / 2.0f;
    if (chevron_hover_) {
        const float h = static_cast<float>(px(11));
        const D2D1_RECT_F bg{cx - h, cy - h, cx + h, cy + h};
        const float radius = static_cast<float>(px(settings_.corner_radius));
        brush_->SetColor(d2d_colour(theme_.text, theme_.dark ? hover_alpha_dark : hover_alpha_light));
        target_->FillRoundedRectangle(D2D1::RoundedRect(bg, radius, radius), brush_.get());
    }
    const float w = static_cast<float>(px(4));
    const float stroke = (std::max)(1.0f, static_cast<float>(dpi_) / 96.0f * 1.5f);
    brush_->SetColor(d2d_colour(blend(theme_.text, surface_, 0.8f)));
    target_->DrawLine(D2D1::Point2F(cx - w, cy - w / 2.0f), D2D1::Point2F(cx, cy + w / 2.0f), brush_.get(), stroke);
    target_->DrawLine(D2D1::Point2F(cx, cy + w / 2.0f), D2D1::Point2F(cx + w, cy - w / 2.0f), brush_.get(), stroke);
}

const std::uint8_t* StripWindow::pixels(int& width, int& height, int& stride) const noexcept {
    width = width_;
    height = height_;
    stride = buffer_width_ * 4;
    return static_cast<const std::uint8_t*>(bits_);
}

// ---------------------------------------------------------------------------------------------
// Window.

LRESULT CALLBACK StripWindow::window_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<StripWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        self->nccreate_qpc_ = now.QuadPart;
        self->wnd_ = wnd;
        SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<StripWindow*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (self == nullptr) return DefWindowProcW(wnd, msg, wp, lp);
    if (msg == WM_NCDESTROY) {
        SetWindowLongPtrW(wnd, GWLP_USERDATA, 0);
        self->wnd_ = nullptr;
        self->tooltip_ = nullptr; // owned by the strip, so already gone
        self->tip_index_ = no_index;
        self->press_index_ = no_index;
        self->dragging_ = false;
        self->switching_ = false; // the timer died with the window
        self->switch_from_ = no_index;
        return DefWindowProcW(wnd, msg, wp, lp);
    }
    return self->on_message(msg, wp, lp);
}

LRESULT StripWindow::on_message(UINT msg, WPARAM wp, LPARAM lp) noexcept {
    switch (msg) {
    case WM_CREATE: {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        create_qpc_ = now.QuadPart;
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_TIMER:
        if (wp == switch_timer) {
            on_switch_timer();
            return 0;
        }
        break;
    case WM_PAINT: on_paint(); return 0;
    case WM_SIZE: on_size(LOWORD(lp), HIWORD(lp)); return 0;
    case WM_MOUSEMOVE: on_mouse_move(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}); return 0;
    case WM_MOUSELEAVE: on_mouse_leave(); return 0;
    case WM_LBUTTONDOWN: on_button_down(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}); return 0;
    case WM_LBUTTONUP: on_button_up(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}); return 0;
    case WM_MBUTTONDOWN: return 0; // no autoscroll
    case WM_MBUTTONUP: on_middle_up(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}); return 0;
    case WM_CAPTURECHANGED:
        // Lost to someone else (a menu, Alt+Tab): a drag in progress is cancelled.
        if (reinterpret_cast<HWND>(lp) != wnd_) {
            if (dragging_) end_drag(false);
            press_index_ = no_index;
        }
        if (listener_ != nullptr) listener_->on_strip_pointer();
        return 0;
    case WM_NOTIFY: {
        // Only the tooltip's text request. Anything below 64 KB is no pointer:
        // foobar2000 windows have been sent WM_NOTIFY with lParam 0 and 0x4E.
        if (lp < 0x10000) break;
        auto* header = reinterpret_cast<NMHDR*>(lp);
        if (header->hwndFrom == tooltip_ && header->code == TTN_GETDISPINFOW) {
            auto* info = reinterpret_cast<NMTTDISPINFOW*>(lp);
            info->lpszText = tip_text_.empty() ? const_cast<wchar_t*>(L"") : tip_text_.data();
            return 0;
        }
        break;
    }
    case WM_CONTEXTMENU: on_context_menu(lp); return 0;
    case WM_MOUSEWHEEL: {
        if (!settings_.wheel_cycles || listener_ == nullptr) break;
        wheel_accumulator_ += GET_WHEEL_DELTA_WPARAM(wp);
        while (wheel_accumulator_ >= WHEEL_DELTA) {
            wheel_accumulator_ -= WHEEL_DELTA;
            listener_->on_strip_step(-1);
        }
        while (wheel_accumulator_ <= -WHEEL_DELTA) {
            wheel_accumulator_ += WHEEL_DELTA;
            listener_->on_strip_step(+1);
        }
        return 0;
    }
    case WM_GETDLGCODE: return DLGC_WANTALLKEYS;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE && dragging_) {
            end_drag(false);
            press_index_ = no_index;
            ReleaseCapture();
            return 0;
        }
        if (on_key(wp)) return 0;
        if (listener_ != nullptr && listener_->on_strip_key(msg, wp)) return 0;
        break;
    case WM_SYSKEYDOWN:
        if (listener_ != nullptr && listener_->on_strip_key(msg, wp)) {
            ignore_syschar_ = true;
            return 0;
        }
        break;
    case WM_SYSCHAR:
        if (ignore_syschar_) {
            ignore_syschar_ = false;
            return 0;
        }
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        focused_ = msg == WM_SETFOCUS;
        invalidate_tab(active_);
        if (listener_ != nullptr) listener_->on_strip_pointer();
        break;
    case WM_UPDATEUISTATE: {
        const LRESULT result = DefWindowProcW(wnd_, msg, wp, lp);
        const bool hide = (SendMessageW(wnd_, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS) != 0;
        if (hide != hide_focus_) {
            hide_focus_ = hide;
            invalidate_tab(active_);
        }
        return result;
    }
    case wm_dpichanged_afterparent: check_dpi(); return 0;
    case WM_SETTINGCHANGE:
        if (const bool ct = gfx::system_uses_cleartype(); ct != cleartype_) {
            cleartype_ = ct;
            InvalidateRect(wnd_, nullptr, FALSE);
        }
        break;
    default: break;
    }
    return DefWindowProcW(wnd_, msg, wp, lp);
}

void StripWindow::on_paint() noexcept {
    PAINTSTRUCT ps{};
    const HDC dc = BeginPaint(wnd_, &ps);
    if (dc == nullptr) return;
    const RECT& rc = ps.rcPaint;
    if (rc.right > rc.left && rc.bottom > rc.top) {
        const bool measure = perf::enabled();
        const std::uint64_t start = measure ? perf::now() : 0;
        const std::uint64_t allocations = measure ? perf::allocation_count() : 0;
        if (render(rc)) {
            BitBlt(dc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, mem_dc_, rc.left, rc.top, SRCCOPY);
        } else {
            // Lost target or no Direct2D: flat background now, a full retry next time.
            const COLORREF previous = SetDCBrushColor(dc, surface_);
            FillRect(dc, &rc, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            SetDCBrushColor(dc, previous);
            if (!target_) InvalidateRect(wnd_, nullptr, FALSE);
        }
        if (measure) {
            const auto area = static_cast<std::uint64_t>(rc.right - rc.left) * static_cast<std::uint64_t>(rc.bottom - rc.top);
            stats_.add(perf::elapsed_ms(start, perf::now()), perf::allocation_count() - allocations, area);
        }
    }
    EndPaint(wnd_, &ps);
}

void StripWindow::check_dpi() noexcept {
    if (wnd_ == nullptr) return;
    const unsigned dpi = dpi_override_ != 0 ? dpi_override_ : gfx::window_dpi(wnd_);
    if (dpi == dpi_) return;
    const bool first = dpi_ == 0;
    dpi_ = dpi;
    // The WM_SIZE sent inside CreateWindowExW arrives before create() has set the DPI. That is not
    // a change: create() builds everything next. Notifying here made the container refresh its
    // font against a cold DirectWrite and relayout Columns UI mid-create (0.3.3: 153 ms).
    if (first) return;
    rebuild_text_format();
    rebuild_items();
    update_thickness();
    relayout();
    InvalidateRect(wnd_, nullptr, FALSE);
    if (listener_ != nullptr) listener_->on_strip_metrics_changed();
}

void StripWindow::set_dpi_override(unsigned dpi) noexcept {
    dpi_override_ = dpi;
    check_dpi();
}

void StripWindow::on_size(int width, int height) noexcept {
    check_dpi();
    if (width == width_ && height == height_) return;
    width_ = width;
    height_ = height;
    (void)ensure_buffer(width, height); // here, so WM_PAINT never allocates
    relayout();
    InvalidateRect(wnd_, nullptr, FALSE);
}

void StripWindow::on_mouse_move(POINT pt) noexcept {
    ensure_tooltip();
    if (!tracking_) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, wnd_, 0};
        tracking_ = TrackMouseEvent(&tme) != FALSE;
        if (listener_ != nullptr) listener_->on_strip_pointer();
        if (wnd_ == nullptr) return;
    }
    if (press_index_ != no_index && GetCapture() == wnd_) {
        if (!dragging_ && settings_.drag_reorder && items_.size() > 1) {
            const int dx = GetSystemMetrics(SM_CXDRAG);
            const int dy = GetSystemMetrics(SM_CYDRAG);
            if (std::abs(pt.x - press_pt_.x) > dx || std::abs(pt.y - press_pt_.y) > dy) {
                dragging_ = true;
                stop_switch();
                drag_origin_ = press_index_;
                drag_index_ = press_index_;
                if (tooltip_ != nullptr) SendMessageW(tooltip_, TTM_POP, 0, 0);
            }
        }
        if (dragging_) {
            drag_to(pt);
            return;
        }
    }
    const std::size_t hover = hit_test(pt);
    const bool chevron = chevron_hit(pt);
    if (hover != hover_) {
        const std::size_t old = hover_;
        hover_ = hover;
        invalidate_tab(old);
        invalidate_tab(hover);
        update_tooltip();
    }
    if (chevron != chevron_hover_) {
        chevron_hover_ = chevron;
        const RECT r = chevron_rect();
        InvalidateRect(wnd_, &r, FALSE);
    }
}

void StripWindow::forget_pointer() noexcept {
    if (tooltip_ != nullptr) SendMessageW(tooltip_, TTM_POP, 0, 0);
    if (tracking_ && wnd_ != nullptr) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE | TME_CANCEL, wnd_, 0};
        (void)TrackMouseEvent(&tme);
    }
    tracking_ = false;
    if (hover_ != no_index && !dragging_) {
        const std::size_t old = hover_;
        hover_ = no_index;
        invalidate_tab(old);
        update_tooltip();
    }
    chevron_hover_ = false;
}

void StripWindow::track_pointer() noexcept {
    if (tracking_ || wnd_ == nullptr || !IsWindowVisible(wnd_)) return;
    TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, wnd_, 0};
    tracking_ = TrackMouseEvent(&tme) != FALSE;
}

bool StripWindow::set_layered(bool layered) noexcept {
    if (wnd_ == nullptr) return false;
    if (layered == layered_) return true;
    const LONG_PTR ex = GetWindowLongPtrW(wnd_, GWL_EXSTYLE);
    if (layered) {
        SetWindowLongPtrW(wnd_, GWL_EXSTYLE, ex | WS_EX_LAYERED);
        alpha_ = 255;
        if (!SetLayeredWindowAttributes(wnd_, 0, alpha_, LWA_ALPHA)) {
            SetWindowLongPtrW(wnd_, GWL_EXSTYLE, ex & ~static_cast<LONG_PTR>(WS_EX_LAYERED));
            return false;
        }
    } else {
        SetWindowLongPtrW(wnd_, GWL_EXSTYLE, ex & ~static_cast<LONG_PTR>(WS_EX_LAYERED));
        RedrawWindow(wnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
    }
    layered_ = layered;
    return true;
}

void StripWindow::set_alpha(BYTE alpha) noexcept {
    if (!layered_ || wnd_ == nullptr || alpha == alpha_) return;
    alpha_ = alpha;
    SetLayeredWindowAttributes(wnd_, 0, alpha, LWA_ALPHA);
}

void StripWindow::on_mouse_leave() noexcept {
    tracking_ = false;
    if (listener_ != nullptr) listener_->on_strip_pointer();
    if (wnd_ == nullptr) return;
    if (hover_ != no_index && !dragging_) {
        const std::size_t old = hover_;
        hover_ = no_index;
        invalidate_tab(old);
        update_tooltip();
    }
    if (chevron_hover_) {
        chevron_hover_ = false;
        const RECT r = chevron_rect();
        InvalidateRect(wnd_, &r, FALSE);
    }
}

void StripWindow::on_button_down(POINT pt) noexcept {
    if (listener_ == nullptr) return;
    if (chevron_hit(pt)) {
        const RECT r = chevron_rect();
        POINT screen{horizontal() ? r.left : r.right, horizontal() ? r.bottom : r.top};
        ClientToScreen(wnd_, &screen);
        listener_->on_strip_overflow(screen);
        return;
    }
    const std::size_t index = hit_test(pt);
    if (index == no_index) return;
    // Activating can re-enter (the host sends new tabs); the press is armed only afterwards.
    if (index != active_) listener_->on_strip_activate(index);
    if (wnd_ == nullptr || index >= items_.size()) return;
    press_index_ = index;
    press_pt_ = pt;
    SetCapture(wnd_);
}

void StripWindow::on_button_up(POINT) noexcept {
    const bool had_press = press_index_ != no_index;
    press_index_ = no_index;
    if (dragging_) end_drag(true);
    if (had_press && GetCapture() == wnd_) ReleaseCapture();
}

void StripWindow::on_middle_up(POINT pt) noexcept {
    if (listener_ == nullptr || dragging_) return;
    const std::size_t index = hit_test(pt);
    if (index != no_index) listener_->on_strip_middle_click(index);
}

void StripWindow::drag_to(POINT pt) noexcept {
    const int pos = horizontal() ? pt.x : pt.y;
    // Swap with a neighbour once the pointer passes its middle; unequal widths cannot bounce,
    // because after a swap the pointer sits inside the dragged tab again.
    for (std::size_t guard = 0; guard < items_.size() && drag_index_ < layout_.tabs.size(); ++guard) {
        const std::size_t i = drag_index_;
        if (i > layout_.first) {
            const Span& prev = layout_.tabs[i - 1];
            if (pos < prev.start + prev.length / 2) {
                move_item(i, i - 1);
                drag_index_ = i - 1;
                continue;
            }
        }
        if (i + 1 < layout_.last && i + 1 < layout_.tabs.size()) {
            const Span& next = layout_.tabs[i + 1];
            if (pos > next.start + next.length / 2) {
                move_item(i, i + 1);
                drag_index_ = i + 1;
                continue;
            }
        }
        break;
    }
}

void StripWindow::end_drag(bool commit) noexcept {
    if (!dragging_) return;
    dragging_ = false;
    const std::size_t from = drag_origin_;
    const std::size_t to = drag_index_;
    drag_origin_ = no_index;
    drag_index_ = no_index;
    if (from == no_index || to == no_index || from == to) return;
    if (!commit) {
        move_item(to, from);
        return;
    }
    if (listener_ != nullptr) listener_->on_strip_reorder(from, to);
}

void StripWindow::move_item(std::size_t from, std::size_t to) noexcept {
    if (from >= items_.size() || to >= items_.size() || from == to) return;
    if (from < to) {
        std::rotate(items_.begin() + static_cast<std::ptrdiff_t>(from),
                    items_.begin() + static_cast<std::ptrdiff_t>(from) + 1,
                    items_.begin() + static_cast<std::ptrdiff_t>(to) + 1);
    } else {
        std::rotate(items_.begin() + static_cast<std::ptrdiff_t>(to), items_.begin() + static_cast<std::ptrdiff_t>(from),
                    items_.begin() + static_cast<std::ptrdiff_t>(from) + 1);
    }
    const auto remap = [from, to](std::size_t index) {
        if (index == no_index) return index;
        if (index == from) return to;
        if (from < to && index > from && index <= to) return index - 1;
        if (from > to && index >= to && index < from) return index + 1;
        return index;
    };
    active_ = remap(active_);
    hover_ = remap(hover_);
    relayout();
    if (wnd_ != nullptr) InvalidateRect(wnd_, nullptr, FALSE);
}

void StripWindow::ensure_tooltip() noexcept {
    if (tooltip_ != nullptr || wnd_ == nullptr) return;
    tooltip_ = CreateWindowExW(WS_EX_TRANSPARENT, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, wnd_, nullptr,
                               module_instance(), nullptr);
    if (tooltip_ == nullptr) return;
    TTTOOLINFOW tool{};
    tool.cbSize = sizeof(tool);
    tool.uFlags = TTF_SUBCLASS;
    tool.hwnd = wnd_;
    tool.uId = 1;
    tool.lpszText = LPSTR_TEXTCALLBACKW;
    SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    if (theme_.dark) SetWindowTheme(tooltip_, L"DarkMode_Explorer", nullptr);
}

bool StripWindow::tooltip_wanted(std::size_t index) const noexcept {
    if (index >= items_.size()) return false;
    const Item& item = items_[index];
    if (item.spec.tooltip.empty()) return false;
    return item.spec.label.empty() || item.draw_width < item.text_width || item.spec.tooltip != item.spec.label;
}

void StripWindow::update_tooltip() noexcept {
    if (tooltip_ == nullptr || wnd_ == nullptr) return;
    const std::size_t index = tooltip_wanted(hover_) && !dragging_ ? hover_ : no_index;
    if (index == tip_index_ && index == no_index) return;
    if (index != tip_index_) SendMessageW(tooltip_, TTM_POP, 0, 0);
    tip_index_ = index;
    try {
        tip_text_ = index != no_index ? items_[index].spec.tooltip : std::wstring();
    } catch (...) {
        tip_text_.clear();
    }
    // The tool is the hovered tab; leaving its rectangle is leaving the tool, so the next tab's
    // text is asked for afresh.
    TTTOOLINFOW tool{};
    tool.cbSize = sizeof(tool);
    tool.hwnd = wnd_;
    tool.uId = 1;
    tool.rect = index != no_index ? tab_rect(index) : RECT{};
    SendMessageW(tooltip_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tool));
}

void StripWindow::on_context_menu(LPARAM lp) noexcept {
    if (listener_ == nullptr) return;
    POINT screen{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    std::size_t index = no_index;
    if (screen.x == -1 && screen.y == -1) {
        // Keyboard: open below the active tab.
        index = active_;
        const RECT r = active_ != no_index ? tab_rect(active_) : RECT{};
        screen = POINT{r.left, r.bottom};
        ClientToScreen(wnd_, &screen);
    } else {
        POINT client = screen;
        ScreenToClient(wnd_, &client);
        index = hit_test(client);
    }
    listener_->on_strip_menu(index, screen);
}

bool StripWindow::on_key(WPARAM key) noexcept {
    if (listener_ == nullptr) return false;
    switch (key) {
    case VK_LEFT:
    case VK_UP: listener_->on_strip_step(-1); return true;
    case VK_RIGHT:
    case VK_DOWN: listener_->on_strip_step(+1); return true;
    case VK_HOME:
        if (!items_.empty()) listener_->on_strip_activate(0);
        return true;
    case VK_END:
        if (!items_.empty()) listener_->on_strip_activate(items_.size() - 1);
        return true;
    default: return false;
    }
}

} // namespace bettertabs
