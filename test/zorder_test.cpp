// What can bury the auto-hide hot zone (a layered child at alpha 1) under a sibling panel, and
// what tells the parent about it? Fullscreen visualisations reparent their window to a popup and
// back with SetParent; afterwards the strip could no longer be revealed (0.5.3).
// Needs a Windows 8+ manifest (compat.manifest): without one, layered children fail to create.
#include <windows.h>
#include <cstdio>

static HWND g_top, g_panel, g_zone;
static int g_reorder, g_parentchange;

static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM wp, LPARAM lp) { return DefWindowProcW(w, m, wp, lp); }

static void CALLBACK on_event(HWINEVENTHOOK, DWORD ev, HWND hwnd, LONG obj, LONG, DWORD, DWORD) {
    if (obj != OBJID_WINDOW) return;
    if (ev == EVENT_OBJECT_REORDER && hwnd == g_top) ++g_reorder;
    if (ev == EVENT_OBJECT_PARENTCHANGE && hwnd == g_panel && GetAncestor(hwnd, GA_PARENT) == g_top) {
        ++g_parentchange;
        // What the component does: put the hot zone back on top.
        SetWindowPos(g_zone, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

static void pump() {
    MSG msg;
    for (int i = 0; i < 10; ++i) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        Sleep(10);
    }
}

static bool zone_on_top() { return GetWindow(g_top, GW_CHILD) == g_zone; }

//! `min_parentchange` -1: not checked. The out-of-context callback runs after both SetParent
//! calls returned, so a round trip reports twice, both times with the parent already back.
static int check(const char* what, bool expect_top, int min_parentchange) {
    pump();
    const bool top = zone_on_top();
    std::printf("%-38s zone on top=%d parentchange=%d reorder=%d\n", what, top, g_parentchange, g_reorder);
    const int bad = (top != expect_top) || (min_parentchange >= 0 && g_parentchange < min_parentchange);
    g_reorder = g_parentchange = 0;
    return bad;
}

int main() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.lpszClassName = L"zotest";
    RegisterClassW(&wc);
    g_top = CreateWindowExW(0, L"zotest", L"", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, 420, 340, nullptr,
                            nullptr, nullptr, nullptr);
    g_panel = CreateWindowExW(0, L"zotest", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 400, 300, g_top,
                              nullptr, nullptr, nullptr);
    g_zone = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOPARENTNOTIFY, L"zotest", L"", WS_CHILD | WS_CLIPSIBLINGS, 0,
                             0, 400, 6, g_top, nullptr, nullptr, nullptr);
    if (g_zone == nullptr) {
        std::printf("layered child not created (%lu): manifest missing?\n", GetLastError());
        return 1;
    }
    SetLayeredWindowAttributes(g_zone, 0, 1, LWA_ALPHA);
    ShowWindow(g_zone, SW_SHOWNA);
    SetWindowPos(g_zone, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    int bad = check("baseline", true, 0);

    // Unwatched: SetParent away and back buries the zone, silently.
    SetParent(g_panel, nullptr);
    SetParent(g_panel, g_top);
    pump();
    const bool buried = !zone_on_top();
    std::printf("%-38s zone on top=%d (expected 0)\n", "SetParent away and back, unwatched", !buried);
    bad |= !buried;
    SetWindowPos(g_zone, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    g_reorder = g_parentchange = 0;

    const HWINEVENTHOOK h1 = SetWinEventHook(EVENT_OBJECT_REORDER, EVENT_OBJECT_REORDER, nullptr, on_event,
                                             GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
    const HWINEVENTHOOK h2 = SetWinEventHook(EVENT_OBJECT_PARENTCHANGE, EVENT_OBJECT_PARENTCHANGE, nullptr, on_event,
                                             GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
    if (h1 == nullptr || h2 == nullptr) {
        std::printf("SetWinEventHook failed\n");
        return 1;
    }

    // Watched: the PARENTCHANGE for the return trip puts the zone back on top.
    SetParent(g_panel, nullptr);
    SetParent(g_panel, g_top);
    bad |= check("SetParent away and back, watched", true, 1);

    // Not covered by the watch (no event at all): the WM_SETCURSOR fallback has to catch it.
    std::printf("(observed, not asserted)\n");
    SetWindowPos(g_panel, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    check("panel SetWindowPos(HWND_TOP)", false, -1);

    UnhookWinEvent(h1);
    UnhookWinEvent(h2);
    DestroyWindow(g_top);
    return bad;
}
