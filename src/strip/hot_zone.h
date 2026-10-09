#pragma once

// The auto-hide hot zone: a thin child window along the strip's edge that reports
// the pointer entering and leaving it (TrackMouseEvent, no polling) and clicks. On Windows 8+ it
// is a layered child at alpha 1: invisible, still hit-testable, composed over the panel without
// touching it. Where layered children are unavailable it is an ordinary child that paints a solid
// colour, and the host reserves its room instead of covering the panel. No SDK dependency.

#include <windows.h>

namespace bettertabs {

class HotZoneListener {
public:
    //! The pointer entered (inside) or left the zone; `clicked` for a button press in it.
    virtual void on_hot_zone(bool inside, bool clicked) noexcept = 0;

protected:
    ~HotZoneListener() = default;
};

class HotZone {
public:
    HotZone() = default;
    HotZone(const HotZone&) = delete;
    HotZone& operator=(const HotZone&) = delete;
    ~HotZone() { destroy(); }

    //! Hidden after creation. `layered` asks for the invisible Windows 8+ form; layered() says
    //! whether it was granted (child layered windows can be refused).
    bool create(HWND parent, HotZoneListener& listener, bool layered) noexcept;
    void destroy() noexcept;
    [[nodiscard]] HWND hwnd() const noexcept { return wnd_; }
    [[nodiscard]] bool layered() const noexcept { return layered_; }
    //! The colour painted when not layered.
    void set_colour(COLORREF colour) noexcept;
    //! When not layered: paint what the parent paints behind it (over the colour) instead.
    void set_transparent(bool transparent) noexcept;
    //! Call when hiding it: a window hidden under the pointer gets no WM_MOUSELEAVE.
    void forget_pointer() noexcept { tracking_ = false; }

private:
    static LRESULT CALLBACK window_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) noexcept;
    LRESULT on_message(UINT msg, WPARAM wp, LPARAM lp) noexcept;

    HWND wnd_{nullptr};
    HotZoneListener* listener_{nullptr};
    bool layered_{false};
    bool tracking_{false};
    COLORREF colour_{RGB(0, 0, 0)};
    bool transparent_{false};
};

} // namespace bettertabs
