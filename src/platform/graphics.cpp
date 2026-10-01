#include <windows.h>

#include "graphics.h"

#include <dwrite_2.h>

#include "com_ptr.h"

#include <atomic>
#include <cstdio>

namespace bettertabs::gfx {

namespace {

ID2D1Factory* g_d2d = nullptr;
IDWriteFactory* g_dwrite = nullptr;
bool g_d2d_tried = false;
bool g_dwrite_tried = false;

using GetDpiForWindowFn = UINT(WINAPI*)(HWND);

[[nodiscard]] GetDpiForWindowFn get_dpi_for_window() noexcept {
    static const GetDpiForWindowFn fn = [] {
        const HMODULE user32 = GetModuleHandleW(L"user32.dll");
        return user32 != nullptr ? reinterpret_cast<GetDpiForWindowFn>(
                                       reinterpret_cast<void*>(GetProcAddress(user32, "GetDpiForWindow")))
                                 : nullptr;
    }();
    return fn;
}

} // namespace

ID2D1Factory* d2d() noexcept {
    if (g_d2d == nullptr && !g_d2d_tried) {
        g_d2d_tried = true;
        D2D1_FACTORY_OPTIONS options{};
        (void)D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), &options,
                                reinterpret_cast<void**>(&g_d2d));
    }
    return g_d2d;
}

IDWriteFactory* dwrite() noexcept {
    if (g_dwrite == nullptr && !g_dwrite_tried) {
        g_dwrite_tried = true;
        (void)DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                  reinterpret_cast<IUnknown**>(&g_dwrite));
    }
    return g_dwrite;
}

void shutdown() noexcept {
    if (g_d2d != nullptr) g_d2d->Release();
    if (g_dwrite != nullptr) g_dwrite->Release();
    g_d2d = nullptr;
    g_dwrite = nullptr;
}

unsigned system_dpi() noexcept {
    static const unsigned dpi = [] {
        const HDC screen = GetDC(nullptr);
        const int value = screen != nullptr ? GetDeviceCaps(screen, LOGPIXELSX) : 96;
        if (screen != nullptr) ReleaseDC(nullptr, screen);
        return value > 0 ? static_cast<unsigned>(value) : 96u;
    }();
    return dpi;
}

unsigned window_dpi(HWND wnd) noexcept {
    if (const GetDpiForWindowFn fn = get_dpi_for_window(); fn != nullptr && wnd != nullptr) {
        const UINT dpi = fn(wnd);
        if (dpi != 0) return dpi;
    }
    return system_dpi();
}

bool layered_children_supported() noexcept {
    // IsWindows8OrGreater needs a manifest to tell the truth; a Windows 8 export does not.
    static const bool value = [] {
        const HMODULE user32 = GetModuleHandleW(L"user32.dll");
        return user32 != nullptr && GetProcAddress(user32, "GetPointerType") != nullptr;
    }();
    return value;
}

bool colour_fonts_supported() noexcept {
    // An older Direct2D rejects the unknown flag at EndDraw, so ask the DLL: this export is 8.1+.
    static const bool value = [] {
        const HMODULE d2d1 = GetModuleHandleW(L"d2d1.dll");
        return d2d1 != nullptr && GetProcAddress(d2d1, "D2D1ComputeMaximumScaleFactor") != nullptr;
    }();
    return value;
}

namespace {
// 0 never started, 1 running, 2 done (g_warm_us valid).
std::atomic<int> g_warm_state{0};
std::atomic<long long> g_warm_us{0};
} // namespace

void warm_text_status(char* out, unsigned size) noexcept {
    if (out == nullptr || size == 0) return;
    const int state = g_warm_state.load();
    if (state == 0) {
        std::snprintf(out, size, "none");
    } else if (state == 1) {
        std::snprintf(out, size, "running");
    } else {
        std::snprintf(out, size, "%.3f ms", static_cast<double>(g_warm_us.load()) / 1000.0);
    }
}

double warm_text() noexcept {
    g_warm_state.store(1);
    LARGE_INTEGER freq{};
    LARGE_INTEGER start{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    try {
        com_ptr<IDWriteFactory> factory;
        if (SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                          reinterpret_cast<IUnknown**>(factory.put())))) {
            com_ptr<IDWriteFontCollection> fonts;
            (void)factory->GetSystemFontCollection(fonts.put(), FALSE);
            // Windows 8.1+: the system fallback Columns UI builds its emoji fallback from.
            com_ptr<IDWriteFactory2> factory2;
            if (SUCCEEDED(factory->QueryInterface(__uuidof(IDWriteFactory2), reinterpret_cast<void**>(factory2.put())))) {
                com_ptr<IDWriteFontFallback> fallback;
                (void)factory2->GetSystemFontFallback(fallback.put());
            }
            NONCLIENTMETRICSW ncm{};
            ncm.cbSize = sizeof(ncm);
            const wchar_t* family = L"Segoe UI";
            if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0) && ncm.lfMessageFont.lfFaceName[0] != L'\0') {
                family = ncm.lfMessageFont.lfFaceName;
            }
            com_ptr<IDWriteTextFormat> format;
            if (SUCCEEDED(factory->CreateTextFormat(family, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                                    DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"", format.put()))) {
                com_ptr<IDWriteTextLayout> layout;
                if (SUCCEEDED(factory->CreateTextLayout(L"Ag", 2, format.get(), 1000.0f, 1000.0f, layout.put()))) {
                    DWRITE_TEXT_METRICS metrics{};
                    (void)layout->GetMetrics(&metrics);
                }
            }
            com_ptr<IDWriteRenderingParams> params;
            (void)factory->CreateMonitorRenderingParams(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY),
                                                        params.put());
        }
    } catch (...) {
    }
    LARGE_INTEGER end{};
    QueryPerformanceCounter(&end);
    const double ms = freq.QuadPart > 0
                          ? static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart)
                          : 0.0;
    g_warm_us.store(static_cast<long long>(ms * 1000.0));
    g_warm_state.store(2);
    return ms;
}

bool system_uses_cleartype() noexcept {
    BOOL smoothing = FALSE;
    UINT type = 0;
    if (!SystemParametersInfoW(SPI_GETFONTSMOOTHING, 0, &smoothing, 0) || !smoothing) return false;
    return SystemParametersInfoW(SPI_GETFONTSMOOTHINGTYPE, 0, &type, 0) &&
           type == FE_FONTSMOOTHINGCLEARTYPE;
}

} // namespace bettertabs::gfx
