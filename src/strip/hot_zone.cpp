#include "hot_zone.h"

#include <uxtheme.h>

#pragma comment(lib, "uxtheme.lib")

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace bettertabs {

namespace {

constexpr wchar_t class_name[] = L"foo_bettertabs_hot_zone";

[[nodiscard]] HINSTANCE module_instance() noexcept { return reinterpret_cast<HINSTANCE>(&__ImageBase); }

} // namespace

bool HotZone::create(HWND parent, HotZoneListener& listener, bool layered) noexcept {
    if (wnd_ != nullptr) return true;
    static const ATOM atom = [] {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &HotZone::window_proc;
        wc.hInstance = module_instance();
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = class_name;
        return RegisterClassExW(&wc);
    }();
    if (atom == 0) return false;
    listener_ = &listener;
    const DWORD base_ex = WS_EX_NOPARENTNOTIFY;
    const DWORD style = WS_CHILD | WS_CLIPSIBLINGS;
    HWND wnd = nullptr;
    if (layered) {
        wnd = CreateWindowExW(base_ex | WS_EX_LAYERED, class_name, L"", style, 0, 0, 0, 0, parent, nullptr,
                              module_instance(), this);
        // Alpha 1, not 0: still hit-testable, never visible.
        if (wnd != nullptr && !SetLayeredWindowAttributes(wnd, 0, 1, LWA_ALPHA)) {
            DestroyWindow(wnd);
            wnd = nullptr;
        }
    }
    layered_ = wnd != nullptr;
    if (wnd == nullptr) {
        wnd = CreateWindowExW(base_ex, class_name, L"", style, 0, 0, 0, 0, parent, nullptr, module_instance(), this);
    }
    if (wnd == nullptr) {
        listener_ = nullptr;
        return false;
    }
    return true;
}

void HotZone::destroy() noexcept {
    if (wnd_ != nullptr) DestroyWindow(wnd_);
    wnd_ = nullptr;
    tracking_ = false;
}

void HotZone::set_colour(COLORREF colour) noexcept {
    if (colour == colour_) return;
    colour_ = colour;
    if (wnd_ != nullptr && !layered_) InvalidateRect(wnd_, nullptr, TRUE);
}

void HotZone::set_transparent(bool transparent) noexcept {
    if (transparent == transparent_) return;
    transparent_ = transparent;
    if (wnd_ != nullptr && !layered_) InvalidateRect(wnd_, nullptr, TRUE);
}

LRESULT CALLBACK HotZone::window_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<HotZone*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->wnd_ = wnd;
        SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<HotZone*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (self == nullptr) return DefWindowProcW(wnd, msg, wp, lp);
    if (msg == WM_NCDESTROY) {
        SetWindowLongPtrW(wnd, GWLP_USERDATA, 0);
        if (self->wnd_ == wnd) self->wnd_ = nullptr;
        return DefWindowProcW(wnd, msg, wp, lp);
    }
    return self->on_message(msg, wp, lp);
}

LRESULT HotZone::on_message(UINT msg, WPARAM wp, LPARAM lp) noexcept {
    switch (msg) {
    case WM_MOUSEMOVE:
        if (!tracking_) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, wnd_, 0};
            tracking_ = TrackMouseEvent(&tme) != FALSE;
            if (listener_ != nullptr) listener_->on_hot_zone(true, false);
        }
        return 0;
    case WM_MOUSELEAVE:
        tracking_ = false;
        if (listener_ != nullptr) listener_->on_hot_zone(false, false);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        if (listener_ != nullptr) listener_->on_hot_zone(true, true);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        const HDC dc = BeginPaint(wnd_, &ps);
        if (dc != nullptr) {
            const COLORREF previous = SetDCBrushColor(dc, colour_);
            FillRect(dc, &ps.rcPaint, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            SetDCBrushColor(dc, previous);
            if (transparent_) DrawThemeParentBackground(wnd_, dc, &ps.rcPaint);
        }
        EndPaint(wnd_, &ps);
        return 0;
    }
    default: break;
    }
    return DefWindowProcW(wnd_, msg, wp, lp);
}

} // namespace bettertabs
