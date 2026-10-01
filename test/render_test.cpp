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
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGR;
    ok = ok && SUCCEEDED(frame->SetPixelFormat(&format)) &&
         SUCCEEDED(frame->WritePixels(static_cast<UINT>(canvas.height), static_cast<UINT>(canvas.width * 4),
                                      static_cast<UINT>(canvas.px.size() * 4),
                                      reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(canvas.px.data())))) &&
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
        {"light underline", false, Indicator::underline, false, no_index, 560},
        {"light pill", false, Indicator::pill, false, 1, 560},
        {"light chips", false, Indicator::none, true, no_index, 560},
        {"dark icons", true, Indicator::underline, false, no_index, 560, StripPosition::top, SideText::horizontal, 1},
        {"light icon pill", false, Indicator::pill, false, no_index, 560, StripPosition::top, SideText::horizontal, 1},
    };
    const std::vector<Look> sides = {
        {"dark left", true, Indicator::underline, false, 1, 260, StripPosition::left, SideText::horizontal},
        {"dark left rotated", true, Indicator::pill, false, no_index, 400, StripPosition::left, SideText::rotated},
        {"light right rotated", false, Indicator::underline, false, no_index, 400, StripPosition::right,
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
    DestroyWindow(parent);
    gfx::shutdown();
    CoUninitialize();
    std::printf("failures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
