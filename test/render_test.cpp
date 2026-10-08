// Offline render test: draws the real StripWindow (same code as the component, no foobar2000)
// into PNGs, one per DPI, with a row per look: dark and light, underline / pill / chips, a hovered
// tab, an overflowing strip, and side strips. For the README and for pixel checks (snapped text,
// crisp edges). Built and run by test\build_tests.bat; images go to test\out\render_*.png.
//
// Also reports paint time and allocations per full render as a rough budget check.

#include <windows.h>

#include <wincodec.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../src/model/colour.h"
#include "../src/platform/graphics.h"
#include "../src/platform/perf.h"
#include "../src/strip/strip_window.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

// perf.cpp needs the SDK; the strip only needs these.
namespace bettertabs::perf {
bool enabled() noexcept { return false; }
bool use_setredraw() noexcept { return false; }
std::uint64_t allocation_count() noexcept { return 0; }
std::uint64_t now() noexcept {
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    return static_cast<std::uint64_t>(c.QuadPart);
}
double elapsed_ms(std::uint64_t a, std::uint64_t b) noexcept {
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    return static_cast<double>(b - a) * 1000.0 / static_cast<double>(f.QuadPart);
}
} // namespace bettertabs::perf

using namespace bettertabs;

namespace {

class NullListener : public StripListener {
public:
    void on_strip_activate(std::size_t) noexcept override {}
    void on_strip_step(int) noexcept override {}
    void on_strip_menu(std::size_t, POINT) noexcept override {}
    void on_strip_overflow(POINT) noexcept override {}
    bool on_strip_key(UINT, WPARAM) noexcept override { return false; }
    void on_strip_metrics_changed() noexcept override {}
    void on_strip_middle_click(std::size_t) noexcept override {}
    void on_strip_reorder(std::size_t, std::size_t) noexcept override {}
    void on_strip_reorder_block(std::span<const std::size_t>, std::size_t, bool) noexcept override {}
};

struct Canvas {
    int width{0};
    int height{0};
    std::vector<std::uint32_t> px; // 0x00RRGGBB

    void init(int w, int h, std::uint32_t fill) {
        width = w;
        height = h;
        px.assign(static_cast<std::size_t>(w) * h, fill);
    }
    void blit(const std::uint8_t* src, int w, int h, int stride, int x0, int y0) {
        for (int y = 0; y < h && y0 + y < height; ++y) {
            const auto* row = reinterpret_cast<const std::uint32_t*>(src + static_cast<std::size_t>(y) * stride);
            for (int x = 0; x < w && x0 + x < width; ++x) {
                px[static_cast<std::size_t>(y0 + y) * width + x0 + x] = row[x] & 0xFFFFFFu;
            }
        }
    }
    void fill(int x0, int y0, int w, int h, std::uint32_t c) {
        for (int y = y0; y < y0 + h && y < height; ++y)
            for (int x = x0; x < x0 + w && x < width; ++x) px[static_cast<std::size_t>(y) * width + x] = c;
    }
};

bool save_png(const Canvas& canvas, const wchar_t* path) {
    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory1, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
        return false;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    bool ok = SUCCEEDED(factory->CreateStream(&stream)) &&
              SUCCEEDED(stream->InitializeFromFilename(path, GENERIC_WRITE)) &&
              SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
              SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
              SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
              SUCCEEDED(frame->SetSize(static_cast<UINT>(canvas.width), static_cast<UINT>(canvas.height)));
    // The PNG encoder takes 24 bpp BGR, not 32: SetPixelFormat says so by changing `format`.
    // Rows are written in that format, so the bytes match what the encoder reads.
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    ok = ok && SUCCEEDED(frame->SetPixelFormat(&format)) && IsEqualGUID(format, GUID_WICPixelFormat24bppBGR);
    const UINT stride = static_cast<UINT>(canvas.width * 3);
    std::vector<BYTE> rows(static_cast<std::size_t>(stride) * static_cast<std::size_t>(canvas.height));
    for (std::size_t i = 0; i < canvas.px.size() && i * 3 + 2 < rows.size(); ++i) {
        const std::uint32_t c = canvas.px[i]; // 0x00RRGGBB
        rows[i * 3] = static_cast<BYTE>(c & 0xFF);
        rows[i * 3 + 1] = static_cast<BYTE>((c >> 8) & 0xFF);
        rows[i * 3 + 2] = static_cast<BYTE>((c >> 16) & 0xFF);
    }
    ok = ok &&
         SUCCEEDED(frame->WritePixels(static_cast<UINT>(canvas.height), stride, static_cast<UINT>(rows.size()),
                                      rows.data())) &&
         SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
    if (frame) frame->Release();
    if (encoder) encoder->Release();
    if (stream) stream->Release();
    factory->Release();
    return ok;
}

struct Look {
    const char* name;
    bool dark;
    Indicator indicator;
    bool chip;
    std::size_t hover; // strip index, or no_index
    int width_dip;     // strip length
    StripPosition position{StripPosition::top};
    SideText side{SideText::horizontal};
    //! 0 labels only, 1 icon + label, 2 icons only.
    int icons{0};
    //! Ctrl+click tabs 3 and 4 (with the active tab 1): the selection look.
    bool select{false};
};

//! Fluent/MDL2 code points, and one emoji (U+1F3B5) through the label font's fallback.
const std::vector<std::wstring> icons = {L"\xE8D6", L"\xD83C\xDFB5", L"\xE946", L"\xE8B7", L"\xE8A9", L"\xE80F"};

const std::vector<std::wstring> labels = {L"Artwork view", L"ESLyric", L"Item properties",
                                          L"Album list",   L"Column",  L"Row"};

constexpr std::uint32_t dark_bg = 0x1E1E1E;
constexpr std::uint32_t light_bg = 0xFFFFFF;
//! A cover colour ("teal / orange" from the accent test) through the same legibility step.
constexpr std::uint32_t cover_raw = 0x1F7A80;

int render_dpi(unsigned dpi, HWND parent, NullListener& listener) {
    const std::vector<Look> looks = {
        {"dark underline", true, Indicator::underline, false, no_index, 560},
        {"dark underline hover", true, Indicator::underline, false, 2, 560},
        {"dark pill", true, Indicator::pill, false, 3, 560},
        {"dark chips", true, Indicator::underline, true, no_index, 560},
        {"dark overflow", true, Indicator::underline, false, no_index, 300},
        {"dark tab", true, Indicator::tab, false, 3, 560},
        {"dark outlined tab", true, Indicator::tab_outline, false, 3, 560},
        {"light tab", false, Indicator::tab, false, no_index, 560},
        {"light outlined tab", false, Indicator::tab_outline, false, 2, 560},
        {"dark tab + selection", true, Indicator::tab, false, no_index, 560, StripPosition::top,
         SideText::horizontal, 0, true},
        {"light outline + sel.", false, Indicator::tab_outline, false, no_index, 560, StripPosition::top,
         SideText::horizontal, 0, true},
        {"light underline", false, Indicator::underline, false, no_index, 560},
        {"light pill", false, Indicator::pill, false, 1, 560},
        {"light chips", false, Indicator::none, true, no_index, 560},
        {"dark icons", true, Indicator::underline, false, no_index, 560, StripPosition::top, SideText::horizontal, 1},
        {"light icon pill", false, Indicator::pill, false, no_index, 560, StripPosition::top, SideText::horizontal, 1},
    };
    const std::vector<Look> sides = {
        {"dark left", true, Indicator::underline, false, 1, 260, StripPosition::left, SideText::horizontal},
        {"dark left rotated", true, Indicator::pill, false, no_index, 400, StripPosition::left, SideText::rotated},
        {"light right rotated", false, Indicator::tab_outline, false, no_index, 400, StripPosition::right,
         SideText::rotated},
        {"dark left icons only", true, Indicator::pill, false, no_index, 260, StripPosition::left,
         SideText::horizontal, 2},
    };

    const auto px = [dpi](int dip) { return MulDiv(dip, static_cast<int>(dpi), 96); };
    const int gap = px(8);
    Canvas canvas;
    // Rows of horizontal strips, then the side strips next to each other.
    canvas.init(px(560) + 2 * gap + px(3 * 160), px(560), 0x808080);

    int failures = 0;
    int y = gap;
    double worst_ms = 0.0;
    const auto draw = [&](const Look& look, int x0, int y0, int& used_w, int& used_h) {
        StripWindow strip;
        if (!strip.create(parent, listener)) {
            ++failures;
            return;
        }
        static bool reported = false;
        if (!reported) {
            // A cold first strip in a plain process, to compare with foobar2000's console line.
            reported = true;
            const StripWindow::CreateTimings& st = strip.create_timings();
            std::printf("first strip create: class %.3f ms, window %.3f ms (to WM_NCCREATE %.3f, to WM_CREATE %.3f, "
                        "after %.3f), text %.3f ms\n",
                        st.class_ms, st.window_ms, st.to_nccreate_ms, st.to_create_ms, st.after_create_ms,
                        st.text_ms);
        }
        strip.set_dpi_override(dpi);
        Settings s;
        s.indicator = look.indicator;
        s.chip = look.chip;
        s.position = look.position;
        s.side_text = look.side;
        strip.set_settings(s);
        StripTheme theme;
        const std::uint32_t bg = look.dark ? dark_bg : light_bg;
        theme.background = colour::colorref_from_rgb(bg);
        theme.text = look.dark ? RGB(255, 255, 255) : RGB(0, 0, 0);
        theme.dark = look.dark;
        theme.accent = colour::colorref_from_rgb(colour::accent_for_background(cover_raw, bg));
        strip.set_theme(theme);
        StripFont font;
        font.family = L"Segoe UI";
        font.size_dip = 12.0f; // 9 pt, Columns UI's default label font
        strip.set_font(font);
        if (look.icons == 0) {
            strip.set_labels(labels, 1);
        } else {
            std::vector<StripItem> items(labels.size());
            for (std::size_t i = 0; i < items.size(); ++i) {
                items[i].label = look.icons == 2 ? std::wstring() : labels[i];
                items[i].icon = icons[i % icons.size()];
                items[i].tooltip = labels[i];
            }
            strip.set_items(items, 1);
        }
        const bool horizontal = look.position == StripPosition::top || look.position == StripPosition::bottom;
        const int w = horizontal ? px(look.width_dip) : strip.thickness();
        const int h = horizontal ? strip.thickness() : px(look.width_dip);
        SetWindowPos(strip.hwnd(), nullptr, 0, 0, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
        if (look.select) {
            for (const std::size_t i : {std::size_t{3}, std::size_t{4}}) {
                const RECT r = strip.tab_bounds(i);
                SendMessageW(strip.hwnd(), WM_LBUTTONDOWN, MK_CONTROL | MK_LBUTTON,
                             MAKELPARAM((r.left + r.right) / 2, (r.top + r.bottom) / 2));
                SendMessageW(strip.hwnd(), WM_LBUTTONUP, MK_CONTROL, MAKELPARAM((r.left + r.right) / 2, (r.top + r.bottom) / 2));
            }
            if (strip.selection_count() != 3) {
                std::printf("  %-22s selection %zu, expected 3\n", look.name, strip.selection_count());
                ++failures;
            }
        }
        if (look.hover != no_index) {
            const RECT r = strip.tab_bounds(look.hover);
            SendMessageW(strip.hwnd(), WM_MOUSEMOVE, 0, MAKELPARAM((r.left + r.right) / 2, (r.top + r.bottom) / 2));
        }
        const RECT all{0, 0, w, h};
        const std::uint64_t t0 = perf::now();
        const bool ok = strip.render(all);
        const double ms = perf::elapsed_ms(t0, perf::now());
        // The process's very first render pays for DirectWrite's font cache; not a paint cost.
        static bool warm = false;
        if (warm && ms > worst_ms) worst_ms = ms;
        warm = true;
        int bw = 0, bh = 0, stride = 0;
        const std::uint8_t* bits = strip.pixels(bw, bh, stride);
        if (!ok || bits == nullptr) {
            std::printf("  %-22s render FAILED\n", look.name);
            ++failures;
        } else {
            canvas.blit(bits, bw, bh, stride, x0, y0);
        }
        used_w = w;
        used_h = h;
        strip.destroy();
    };

    for (const Look& look : looks) {
        int w = 0, h = 0;
        draw(look, gap, y, w, h);
        y += h + gap;
    }
    int x = px(560) + 2 * gap;
    for (const Look& look : sides) {
        int w = 0, h = 0;
        draw(look, x, gap, w, h);
        x += w + gap;
    }
    canvas.height = (std::max)(y, px(400) + 2 * gap);
    canvas.px.resize(static_cast<std::size_t>(canvas.width) * canvas.height);

    wchar_t path[MAX_PATH];
    swprintf_s(path, L"out\\render_%u.png", dpi * 100 / 96);
    if (!save_png(canvas, path)) {
        std::printf("  could not write %ls\n", path);
        ++failures;
    }
    std::printf("dpi %u (%u%%): %zu strips, worst full render %.3f ms -> %ls\n", dpi, dpi * 100 / 96,
                looks.size() + sides.size(), worst_ms, path);
    if (worst_ms > 8.0) ++failures;
    return failures;
}

class DragListener : public NullListener {
public:
    std::vector<std::size_t> moved;
    std::size_t neighbour{no_index};
    bool before{true};
    int blocks{0};
    int singles{0};
    void on_strip_reorder(std::size_t, std::size_t) noexcept override { ++singles; }
    void on_strip_reorder_block(std::span<const std::size_t> m, std::size_t n, bool b) noexcept override {
        moved.assign(m.begin(), m.end());
        neighbour = n;
        before = b;
        ++blocks;
    }
};

//! Dragging a selected tab moves the selection as a block.
int block_drag_test(HWND parent) {
    int failures = 0;
    const auto check = [&failures](bool ok, const char* what) {
        std::printf("%s  block drag: %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    DragListener listener;
    StripWindow strip;
    if (!strip.create(parent, listener)) return 1;
    strip.set_dpi_override(96);
    strip.set_settings(Settings{});
    StripFont font;
    font.family = L"Segoe UI";
    font.size_dip = 12.0f;
    strip.set_font(font);
    std::vector<std::wstring> names;
    for (wchar_t c = L'A'; c < L'A' + 6; ++c) names.push_back(std::wstring(L"Tab ") + c);
    strip.set_labels(names, 0);
    SetWindowPos(strip.hwnd(), nullptr, 0, 0, 900, strip.thickness(), SWP_NOZORDER | SWP_NOACTIVATE);
    const auto centre = [&strip](std::size_t i) {
        const RECT r = strip.tab_bounds(i);
        return MAKELPARAM((r.left + r.right) / 2, (r.top + r.bottom) / 2);
    };
    const auto click = [&strip](LPARAM at, WPARAM keys) {
        SendMessageW(strip.hwnd(), WM_LBUTTONDOWN, keys | MK_LBUTTON, at);
        SendMessageW(strip.hwnd(), WM_LBUTTONUP, keys, at);
    };
    click(centre(1), MK_CONTROL);
    click(centre(3), MK_CONTROL);
    check(strip.selection_count() == 3, "tabs 0, 1, 3 selected");
    // Grab tab 3 and drag it past the last tab: [2 4 5 0 1 3].
    const RECT grabbed = strip.tab_bounds(3);
    const RECT last = strip.tab_bounds(5);
    const int y = (grabbed.top + grabbed.bottom) / 2;
    SendMessageW(strip.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, centre(3));
    check(strip.selection_count() == 3, "pressing a selected tab keeps the selection");
    SendMessageW(strip.hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(grabbed.left + 20, y));
    SendMessageW(strip.hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(last.right + 400, y));
    check(strip.dragging(), "dragging");
    SendMessageW(strip.hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(last.right + 400, y));
    check(listener.blocks == 1 && listener.singles == 0, "one block reorder");
    check(listener.moved == std::vector<std::size_t>{0, 1, 3}, "moved 0, 1, 3");
    check(listener.neighbour == 5 && !listener.before, "after tab 5");
    check(strip.selection_count() == 3, "the selection stays after the drag");
    // The host makes it real and sends the tabs in the new order.
    const std::vector<std::wstring> moved_names = {names[2], names[4], names[5], names[0], names[1], names[3]};
    strip.set_labels(moved_names, 3);
    check(strip.selection_count() == 3 && strip.is_selected(3) && strip.is_selected(4) && strip.is_selected(5),
          "the selection follows the moved tabs");
    // Escape cancels: the order comes back, nothing is reported.
    const RECT before_drag = strip.tab_bounds(4);
    SendMessageW(strip.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, centre(4));
    SendMessageW(strip.hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(before_drag.left - 20, y));
    SendMessageW(strip.hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(2, y));
    check(strip.dragging(), "dragging again");
    SendMessageW(strip.hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
    SendMessageW(strip.hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(2, y));
    check(listener.blocks == 1 && !strip.dragging(), "Esc cancels");
    const RECT back = strip.tab_bounds(4);
    check(back.left == before_drag.left && back.right == before_drag.right && strip.is_selected(3) &&
              strip.is_selected(4) && strip.is_selected(5),
          "Esc restores the order");
    // A click on empty strip space ends the selection.
    {
        RECT client{};
        GetClientRect(strip.hwnd(), &client);
        const RECT end = strip.tab_bounds(5);
        if (client.right - end.right > 4) {
            click(MAKELPARAM(client.right - 2, (client.top + client.bottom) / 2), 0);
            check(strip.selection_count() == 0, "a click on empty space clears the selection");
        } else {
            check(false, "no empty space to click");
        }
    }
    click(centre(1), MK_CONTROL);
    check(strip.selection_count() == 2, "Ctrl+click selects again");
    SendMessageW(strip.hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
    check(strip.selection_count() == 0, "Esc clears the selection");
    click(centre(1), MK_CONTROL);
    click(centre(2), MK_SHIFT);
    check(strip.selection_count() == 2, "Shift+click selects a range");
    // A plain click on a selected tab ends the selection on release.
    click(centre(1), 0);
    check(strip.selection_count() == 0, "a click clears the selection");
    strip.destroy();
    return failures;
}

//! Activates like the host does: the clicked tab becomes active through set_active().
class ActivatingListener : public NullListener {
public:
    StripWindow* strip{nullptr};
    void on_strip_activate(std::size_t index) noexcept override {
        if (strip != nullptr) strip->set_active(index);
    }
};

//! Shift+click ranges, tabs added or removed, focus loss and a press without its button-up.
int selection_test(HWND parent) {
    int failures = 0;
    const auto check = [&failures](bool ok, const char* what) {
        std::printf("%s  selection: %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    ActivatingListener listener;
    StripWindow strip;
    listener.strip = &strip;
    if (!strip.create(parent, listener)) return 1;
    strip.set_dpi_override(96);
    strip.set_settings(Settings{});
    StripFont font;
    font.family = L"Segoe UI";
    font.size_dip = 12.0f;
    strip.set_font(font);
    std::vector<std::wstring> names;
    for (wchar_t c = L'A'; c < L'A' + 6; ++c) names.push_back(std::wstring(L"Tab ") + c);
    strip.set_labels(names, 0);
    SetWindowPos(strip.hwnd(), nullptr, 0, 0, 1200, strip.thickness(), SWP_NOZORDER | SWP_NOACTIVATE);
    const auto centre = [&strip](std::size_t i) {
        const RECT r = strip.tab_bounds(i);
        return MAKELPARAM((r.left + r.right) / 2, (r.top + r.bottom) / 2);
    };
    const auto click = [&strip](LPARAM at, WPARAM keys) {
        SendMessageW(strip.hwnd(), WM_LBUTTONDOWN, keys | MK_LBUTTON, at);
        SendMessageW(strip.hwnd(), WM_LBUTTONUP, keys, at);
    };
    // Click tab 4, then a new tab appears at the end and becomes active.
    click(centre(4), 0);
    check(strip.active() == 4, "a click activates");
    names.push_back(L"Tab G");
    strip.set_labels(names, 6);
    // Shift+click tab 1: the range runs from the new active tab, not the tab clicked before.
    click(centre(1), MK_SHIFT);
    bool range = strip.selection_count() == 6;
    for (std::size_t i = 1; i <= 6; ++i) range = range && strip.is_selected(i);
    check(range, "Shift+click after a new active tab selects 1..6");
    // A tab less: the indices under the selection moved, so it ends.
    names.pop_back();
    strip.set_labels(names, 5);
    check(strip.selection_count() == 0, "a removed tab clears the selection");
    // Keyboard focus going elsewhere ends the selection.
    click(centre(2), MK_CONTROL);
    check(strip.selection_count() == 2, "Ctrl+click selects");
    SendMessageW(strip.hwnd(), WM_KILLFOCUS, 0, 0);
    check(strip.selection_count() == 0, "losing the focus clears the selection");
    // Ctrl+click keeps the anchor at the clicked tab while the active tab stays.
    click(centre(1), MK_CONTROL);
    click(centre(3), MK_SHIFT);
    check(strip.selection_count() == 3 && strip.is_selected(1) && strip.is_selected(3) && !strip.is_selected(5),
          "Shift+click after Ctrl+click ranges from the Ctrl+clicked tab");
    // A press on a selected tab whose button-up never comes (capture taken by a menu).
    SendMessageW(strip.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, centre(2));
    SendMessageW(strip.hwnd(), WM_CAPTURECHANGED, 0, 0);
    check(strip.selection_count() == 0, "a lost press ends the selection");
    click(centre(4), MK_CONTROL);
    check(strip.selection_count() == 2, "the next Ctrl+click selection survives its button-up");
    strip.destroy();
    return failures;
}

//! The hover styles (Settings::hover_*): what each draws on a hovered inactive tab, that the
//! active tab keeps its own look, and the fade. Also writes out\hover_96.png, a row per style.
int hover_test(HWND parent) {
    int failures = 0;
    const auto check = [&failures](bool ok, const char* what) {
        std::printf("%s  hover: %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    const std::vector<std::wstring> names = {L"Alpha", L"Bravo", L"Charlie", L"Delta"};
    constexpr std::uint32_t bg = dark_bg;
    struct Shot {
        int w{0};
        int h{0};
        std::vector<std::uint32_t> px;
        RECT tab{};
        [[nodiscard]] std::uint32_t at(int x, int y) const {
            if (x < 0 || y < 0 || x >= w || y >= h) return dark_bg;
            return px[static_cast<std::size_t>(y) * w + x];
        }
        //! Pixels of `r` that are not the strip's background.
        [[nodiscard]] int marked(const RECT& r) const {
            int n = 0;
            for (int y = r.top; y < r.bottom; ++y)
                for (int x = r.left; x < r.right; ++x) n += at(x, y) != dark_bg ? 1 : 0;
            return n;
        }
    };
    NullListener listener;
    // Renders the strip with `hover` hovered (no_index: none). `fade_wait_ms` > 0 pumps messages
    // that long after the hover (the fade timer) before rendering; < 0 renders straight away.
    const auto shoot = [&](const Settings& s, std::size_t hover, int fade_wait_ms = 0) {
        Shot shot;
        StripWindow strip;
        if (!strip.create(parent, listener)) return shot;
        strip.set_dpi_override(96);
        strip.set_settings(s);
        StripTheme theme;
        theme.background = colour::colorref_from_rgb(bg);
        theme.text = RGB(255, 255, 255);
        theme.dark = true;
        theme.accent = colour::colorref_from_rgb(colour::accent_for_background(cover_raw, bg));
        strip.set_theme(theme);
        StripFont font;
        font.family = L"Segoe UI";
        font.size_dip = 12.0f;
        strip.set_font(font);
        strip.set_labels(names, 0);
        const int w = 420;
        const int h = strip.thickness();
        SetWindowPos(strip.hwnd(), nullptr, 0, 0, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
        if (hover != no_index) {
            const RECT r = strip.tab_bounds(hover);
            SendMessageW(strip.hwnd(), WM_MOUSEMOVE, 0, MAKELPARAM((r.left + r.right) / 2, (r.top + r.bottom) / 2));
            shot.tab = r;
        }
        if (fade_wait_ms > 0) {
            const ULONGLONG end = GetTickCount64() + static_cast<ULONGLONG>(fade_wait_ms);
            MSG msg{};
            while (GetTickCount64() < end) {
                // Timers only: the real pointer is elsewhere, and a WM_MOUSELEAVE would end the hover.
                while (PeekMessageW(&msg, strip.hwnd(), WM_TIMER, WM_TIMER, PM_REMOVE)) DispatchMessageW(&msg);
                Sleep(5);
            }
        }
        const RECT all{0, 0, w, h};
        int bw = 0, bh = 0, stride = 0;
        const std::uint8_t* bits = strip.render(all) ? strip.pixels(bw, bh, stride) : nullptr;
        if (bits != nullptr) {
            shot.w = bw;
            shot.h = bh;
            shot.px.resize(static_cast<std::size_t>(bw) * bh);
            for (int y = 0; y < bh; ++y) {
                const auto* row = reinterpret_cast<const std::uint32_t*>(bits + static_cast<std::size_t>(y) * stride);
                for (int x = 0; x < bw; ++x) shot.px[static_cast<std::size_t>(y) * bw + x] = row[x] & 0xFFFFFFu;
            }
        }
        strip.destroy();
        return shot;
    };
    // Bands of the hovered tab (tab 2): its left padding (no text there), the middle of it, and
    // the bottom rows (the underline).
    const auto pad_band = [](const RECT& t) { return RECT{t.left, t.top + 5, t.left + 8, t.bottom - 5}; };
    const auto inner = [](const RECT& t) { return RECT{t.left + 5, (t.top + t.bottom) / 2 - 1, t.left + 8, (t.top + t.bottom) / 2 + 1}; };
    const auto bottom = [](const RECT& t) { return RECT{t.left + 14, t.bottom - 2, t.right - 14, t.bottom}; };

    Canvas canvas;
    canvas.init(420 + 16, 8, 0x808080);
    std::vector<Shot> rows;
    const auto keep = [&](const Shot& shot) {
        if (shot.px.empty()) return;
        rows.push_back(shot);
    };

    Settings base;
    base.indicator = Indicator::pill;
    const Shot none = shoot(base, no_index);
    check(!none.px.empty(), "renders");
    if (none.px.empty()) return failures;

    Settings fill = base;
    fill.hover_style = HoverStyle::fill;
    const Shot fill_shot = shoot(fill, 2);
    keep(fill_shot);
    const RECT t = fill_shot.tab;
    check(none.marked(pad_band(t)) == 0, "no hover: the padding is the strip");
    check(fill_shot.marked(inner(t)) > 0, "fill: the tab is filled");
    check(fill_shot.at(t.left + 6, t.top) == bg, "fill: the gap around the pill stays clear");

    Settings outline = base;
    outline.hover_style = HoverStyle::outline;
    const Shot outline_shot = shoot(outline, 2);
    keep(outline_shot);
    check(outline_shot.marked(inner(t)) == 0, "outline: no fill inside");
    check(outline_shot.marked(pad_band(t)) > 0, "outline: a frame on the side");

    Settings thick = outline;
    thick.hover_line_width = 4;
    thick.hover_colour = HoverColour::accent;
    const Shot thick_shot = shoot(thick, 2);
    keep(thick_shot);
    check(thick_shot.marked(pad_band(t)) > outline_shot.marked(pad_band(t)), "outline: a wider line covers more");

    Settings outline_fill = base;
    outline_fill.hover_style = HoverStyle::outline_fill;
    outline_fill.hover_colour = HoverColour::custom;
    outline_fill.hover_argb = 0xFFE0A030u;
    outline_fill.hover_fill_strength = 20;
    const Shot of_shot = shoot(outline_fill, 2);
    keep(of_shot);
    check(of_shot.marked(inner(t)) > 0 && of_shot.marked(pad_band(t)) > 0, "outline and fill: both");

    Settings underline = base;
    underline.hover_style = HoverStyle::underline;
    underline.hover_colour = HoverColour::accent;
    const Shot ul_shot = shoot(underline, 2);
    keep(ul_shot);
    check(ul_shot.marked(inner(t)) == 0 && ul_shot.marked(bottom(t)) > 0, "underline: a bar at the edge only");

    Settings underline_fill = underline;
    underline_fill.hover_style = HoverStyle::underline_fill;
    keep(shoot(underline_fill, 2));

    Settings mark_none = base;
    mark_none.hover_style = HoverStyle::none;
    mark_none.hover_text = HoverText::colour;
    mark_none.hover_colour = HoverColour::custom;
    mark_none.hover_argb = 0xFFFF4040u;
    const Shot text_shot = shoot(mark_none, 2);
    keep(text_shot);
    check(text_shot.marked(pad_band(t)) == 0, "no mark: nothing around the title");
    int red = 0;
    for (int y = t.top; y < t.bottom; ++y)
        for (int x = t.left; x < t.right; ++x) {
            const std::uint32_t c = text_shot.at(x, y);
            red += ((c >> 16) & 0xFF) > ((c >> 8) & 0xFF) + 60 ? 1 : 0;
        }
    check(red > 0, "no mark: the title takes the hover colour");

    // The active tab (0) keeps its own look: an outline style draws no frame on it.
    const Shot active_shot = shoot(outline, 0);
    const Shot active_plain = shoot(base, 0);
    check(!active_shot.px.empty() && active_shot.px == active_plain.px, "the active tab ignores the hover style");

    // Fade: right after the hover nothing shows yet; after the fade the full mark does.
    Settings fade = fill;
    fade.hover_fade = true;
    fade.hover_fade_ms = 100;
    const Shot fade_start = shoot(fade, 2, -1);
    const Shot fade_end = shoot(fade, 2, 400);
    check(fade_start.marked(inner(t)) == 0, "fade: starts from nothing");
    if (fade_end.px != fill_shot.px) {
        int diff = 0;
        for (std::size_t i = 0; i < fade_end.px.size() && i < fill_shot.px.size(); ++i) diff += fade_end.px[i] != fill_shot.px[i];
        std::printf("      fade end: %d px differ, inner marked %d vs %d, sizes %zu %zu\n", diff,
                    fade_end.marked(inner(t)), fill_shot.marked(inner(t)), fade_end.px.size(), fill_shot.px.size());
    }
    check(fade_end.px == fill_shot.px, "fade: ends at the full mark");

    int y = 8;
    canvas.init(420 + 16, static_cast<int>(rows.size()) * (none.h + 8) + 8, 0x808080);
    for (const Shot& row : rows) {
        std::vector<std::uint8_t> bytes(row.px.size() * 4);
        std::memcpy(bytes.data(), row.px.data(), bytes.size());
        canvas.blit(bytes.data(), row.w, row.h, row.w * 4, 8, y);
        y += row.h + 8;
    }
    if (!save_png(canvas, L"out\\hover_96.png")) check(false, "write out\\hover_96.png");
    return failures;
}

} // namespace

int main() {
    SetProcessDPIAware();
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;
    {
        const double cold = gfx::warm_text();
        const double warm = gfx::warm_text();
        std::printf("text warm-up: cold %.3f ms, again %.3f ms\n", cold, warm);
    }
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"bettertabs_render_test";
    RegisterClassW(&wc);
    const HWND parent = CreateWindowExW(0, wc.lpszClassName, L"", WS_POPUP, 0, 0, 2000, 2000, nullptr, nullptr,
                                        wc.hInstance, nullptr);
    if (parent == nullptr) return 3;
    NullListener listener;
    int failures = 0;
    for (const unsigned dpi : {96u, 144u, 192u}) failures += render_dpi(dpi, parent, listener);
    failures += block_drag_test(parent);
    failures += selection_test(parent);
    failures += hover_test(parent);
    DestroyWindow(parent);
    gfx::shutdown();
    CoUninitialize();
    std::printf("failures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
