// Does showing a child through (Defer)SetWindowPos(SWP_SHOWWINDOW) send WM_SHOWWINDOW?
// Columns UI's splitters show their own children only from WM_SHOWWINDOW (wp TRUE, lp 0), so a
// splitter shown without that message stays empty.
#include <windows.h>
#include <cstdio>

static int g_count = 0;
static LPARAM g_lp = -1;

static LRESULT CALLBACK proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_SHOWWINDOW && wp == TRUE) {
        ++g_count;
        g_lp = lp;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

static HWND make_child(HWND parent) {
    return CreateWindowExW(0, L"swtest", L"", WS_CHILD, 0, 0, 50, 50, parent, nullptr, nullptr, nullptr);
}

static int report(const char* what, int expected) {
    std::printf("%-34s WM_SHOWWINDOW(TRUE) count=%d lp=%lld\n", what, g_count, static_cast<long long>(g_lp));
    const int bad = g_count != expected;
    g_count = 0;
    g_lp = -1;
    return bad;
}

int main() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.lpszClassName = L"swtest";
    RegisterClassW(&wc);
    HWND top = CreateWindowExW(0, L"swtest", L"", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 200, 200, nullptr,
                               nullptr, nullptr, nullptr);
    g_count = 0;
    g_lp = -1;
    int bad = 0;

    HWND a = make_child(top);
    HDWP dwp = BeginDeferWindowPos(1);
    dwp = DeferWindowPos(dwp, a, nullptr, 0, 0, 80, 80, SWP_SHOWWINDOW | SWP_NOZORDER | SWP_NOACTIVATE);
    EndDeferWindowPos(dwp);
    std::printf("(observed behaviour, not asserted)\n");
    report("DeferWindowPos(SWP_SHOWWINDOW)", g_count);

    HWND b = make_child(top);
    SetWindowPos(b, nullptr, 0, 0, 80, 80, SWP_SHOWWINDOW | SWP_NOZORDER | SWP_NOACTIVATE);
    report("SetWindowPos(SWP_SHOWWINDOW)", g_count);

    HWND c = make_child(top);
    ShowWindow(c, SW_SHOWNA);
    bad |= report("ShowWindow(SW_SHOWNA)", 1);

    DestroyWindow(top);
    return bad;
}
