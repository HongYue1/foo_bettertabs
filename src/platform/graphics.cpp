#include <windows.h>

#include "graphics.h"

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

bool system_uses_cleartype() noexcept {
    BOOL smoothing = FALSE;
    UINT type = 0;
    if (!SystemParametersInfoW(SPI_GETFONTSMOOTHING, 0, &smoothing, 0) || !smoothing) return false;
    return SystemParametersInfoW(SPI_GETFONTSMOOTHINGTYPE, 0, &type, 0) &&
           type == FE_FONTSMOOTHINGCLEARTYPE;
}

} // namespace bettertabs::gfx
