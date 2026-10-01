#pragma once

// The tab strip: the only thing this component paints. Its own child window, a sibling of the
// hosted panels, so invalidating it never touches them. Knows nothing about Columns UI: the host
// hands it labels, a theme and a font, and it answers with intents through StripListener. That
// keeps a future Default UI container able to reuse it unchanged.
//
// Painting: one persistent 32 bpp DIB section; a Direct2D DC render target is bound to exactly
// the invalidated rectangle, draws it, and only that rectangle is copied to the screen. Text
// layouts, metrics and tab rectangles are built outside WM_PAINT and only read inside it.

#include <windows.h>

#include <d2d1.h>
#include <dwrite.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "../model/settings.h"
#include "../platform/com_ptr.h"
#include "../platform/perf.h"
#include "strip_layout.h"

namespace bettertabs {

struct StripTheme {
    //! The panel background; the strip is drawn on it (lifted slightly in dark mode).
    COLORREF background{RGB(255, 255, 255)};
    COLORREF text{RGB(0, 0, 0)};
    //! Already made legible against the background by the host.
    COLORREF accent{RGB(0, 120, 215)};
    bool dark{false};
    //! Lift the strip slightly off a dark background (the Columns UI background only; a custom
    //! or tinted strip background is used as given).
    bool lift{true};
    //! Opacity of the active tab's accent fill (pill, chip), 0 = the automatic look.
    float active_fill{0.0f};
    [[nodiscard]] bool operator==(const StripTheme&) const = default;
};

struct StripFont {
    //! GDI description: used when `family` is empty (Columns UI before 3.0).
    LOGFONTW font{};
    //! The DPI lfHeight is expressed in.
    unsigned font_dpi{96};
    //! DirectWrite description from Columns UI 3+; wins over `font` when set.
    std::wstring family;
    DWRITE_FONT_WEIGHT weight{DWRITE_FONT_WEIGHT_NORMAL};
    DWRITE_FONT_STYLE style{DWRITE_FONT_STYLE_NORMAL};
    DWRITE_FONT_STRETCH stretch{DWRITE_FONT_STRETCH_NORMAL};
    float size_dip{0.0f};
    //! An IDWriteFontFallback (Windows 8.1+), applied when the text format can take it.
    com_ptr<IUnknown> fallback;
};

enum class TextAntialias : std::uint8_t { automatic, greyscale, aliased };

//! Columns UI's text rendering options (Colours and fonts > Text rendering).
struct StripTextOptions {
    TextAntialias antialias{TextAntialias::automatic};
    bool gdi_compatible{false};
    bool gdi_natural{false};
    bool colour_glyphs{true};
    com_ptr<IDWriteRenderingParams> params;
};

class StripListener {
public:
    virtual void on_strip_activate(std::size_t index) noexcept = 0;
    //! Arrow keys and the mouse wheel: -1 previous, +1 next.
    virtual void on_strip_step(int direction) noexcept = 0;
    //! Right click or the menu key. index is no_index over empty strip space.
    virtual void on_strip_menu(std::size_t index, POINT screen) noexcept = 0;
    //! The overflow chevron was clicked.
    virtual void on_strip_overflow(POINT screen) noexcept = 0;
    //! Keys the strip does not use itself (Tab, shortcuts). Return true if handled.
    virtual bool on_strip_key(UINT message, WPARAM key) noexcept = 0;
    //! The strip's thickness changed (font, DPI, settings): the host must lay out again.
    virtual void on_strip_metrics_changed() noexcept = 0;

protected:
    ~StripListener() = default;
};

class StripWindow {
public:
    StripWindow() noexcept = default;
    StripWindow(const StripWindow&) = delete;
    StripWindow& operator=(const StripWindow&) = delete;
    ~StripWindow();

    //! Created hidden; the host shows it.
    bool create(HWND parent, StripListener& listener) noexcept;
    void destroy() noexcept;
    [[nodiscard]] HWND hwnd() const noexcept { return wnd_; }

    void set_settings(const Settings& settings) noexcept;
    void set_theme(const StripTheme& theme) noexcept;
    void set_font(const StripFont& font) noexcept;
    void set_text_options(const StripTextOptions& options) noexcept;
    //! Replaces all tabs. Rebuilds their text layouts, so only call it when a label changed.
    void set_labels(std::span<const std::wstring> labels, std::size_t active) noexcept;
    //! Cheap: invalidates the old and new active tab only (unless the visible range moves).
    void set_active(std::size_t active) noexcept;

    //! Thickness across the strip in pixels at the current DPI.
    [[nodiscard]] int thickness() const noexcept { return thickness_; }

    //! Where tab `index` is drawn (empty if it is not), in strip client pixels.
    [[nodiscard]] RECT tab_bounds(std::size_t index) const noexcept { return tab_rect(index); }

    //! Offline render test only: render at this DPI instead of the window's (0 = the window's).
    void set_dpi_override(unsigned dpi) noexcept;

    //! Paint statistics since the last call.
    void take_paint_stats(perf::PaintStats& out) noexcept;

    //! Renders `dirty` into the back buffer. WM_PAINT and the offline render test use it.
    bool render(const RECT& dirty) noexcept;
    //! The back buffer (32 bpp BGRA, top-down, `stride` bytes per row), valid after render().
    [[nodiscard]] const std::uint8_t* pixels(int& width, int& height, int& stride) const noexcept;

private:
    struct Item {
        std::wstring label;
        com_ptr<IDWriteTextLayout> layout;
        int text_width{0};
        int text_height{0};
        //! Text width actually drawn: text_width, or less when the tab is clipped (ellipsis).
        int draw_width{0};
    };

    static LRESULT CALLBACK window_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;
    LRESULT on_message(UINT msg, WPARAM wp, LPARAM lp) noexcept;

    void on_paint() noexcept;
    void on_size(int width, int height) noexcept;
    void on_mouse_move(POINT pt) noexcept;
    void on_mouse_leave() noexcept;
    void on_button_down(POINT pt) noexcept;
    void on_context_menu(LPARAM lp) noexcept;
    bool on_key(WPARAM key) noexcept;
    void check_dpi() noexcept;

    void rebuild_text_format() noexcept;
    [[nodiscard]] bool make_layout(const std::wstring& text, float max_width, float max_height,
                                   IDWriteTextLayout** out) const noexcept;
    [[nodiscard]] D2D1_TEXT_ANTIALIAS_MODE text_antialias() const noexcept;
    void rebuild_items() noexcept;
    void relayout() noexcept;
    void update_thickness() noexcept;
    bool ensure_buffer(int width, int height) noexcept;
    bool ensure_target() noexcept;
    void release_buffer() noexcept;

    [[nodiscard]] bool horizontal() const noexcept;
    //! A side strip with text turned 90 degrees.
    [[nodiscard]] bool rotated() const noexcept;
    [[nodiscard]] RECT tab_rect(std::size_t index) const noexcept;
    [[nodiscard]] RECT chevron_rect() const noexcept;
    [[nodiscard]] std::size_t hit_test(POINT pt) const noexcept;
    [[nodiscard]] bool chevron_hit(POINT pt) const noexcept;
    void invalidate_tab(std::size_t index) noexcept;
    [[nodiscard]] int px(int dips) const noexcept;

    void draw_tab(std::size_t index) noexcept;
    void draw_chevron() noexcept;

    HWND wnd_{nullptr};
    StripListener* listener_{nullptr};

    Settings settings_{};
    StripTheme theme_{};
    StripFont font_{};
    StripTextOptions text_options_{};
    //! The strip's own background, derived from the theme.
    COLORREF surface_{RGB(255, 255, 255)};
    D2D1_DRAW_TEXT_OPTIONS draw_text_options_{D2D1_DRAW_TEXT_OPTIONS_CLIP};
    unsigned dpi_{96};
    unsigned dpi_override_{0};
    int width_{0};
    int height_{0};
    int thickness_{0};

    com_ptr<IDWriteTextFormat> text_format_;
    std::vector<Item> items_;
    std::vector<int> extents_;
    StripLayout layout_;
    int line_height_{0};

    std::size_t active_{no_index};
    std::size_t hover_{no_index};
    bool tracking_{false};
    bool focused_{false};
    bool hide_focus_{true};
    bool chevron_hover_{false};
    //! A shortcut consumed WM_SYSKEYDOWN; swallow the WM_SYSCHAR that follows (no beep).
    bool ignore_syschar_{false};
    int wheel_accumulator_{0};

    // Back buffer and target.
    HDC mem_dc_{nullptr};
    HBITMAP dib_{nullptr};
    HGDIOBJ old_bitmap_{nullptr};
    void* bits_{nullptr};
    int buffer_width_{0};
    int buffer_height_{0};
    com_ptr<ID2D1DCRenderTarget> target_;
    com_ptr<ID2D1SolidColorBrush> brush_;
    bool cleartype_{false};
    //! Top-left of the rectangle being rendered; drawing code works in client pixels.
    float origin_x_{0.0f};
    float origin_y_{0.0f};

    perf::PaintStats stats_{};
};

} // namespace bettertabs
