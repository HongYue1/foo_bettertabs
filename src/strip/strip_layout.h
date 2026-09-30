#pragma once

// Where the tabs go along the strip. One dimension only ("along" = x for a top/bottom strip,
// y for a side strip), in whole device pixels, so edges are always crisp. Pure: no Win32, no
// allocation once `out` has grown to the tab count. test/layout_test.cpp covers it offline.

#include <cstddef>
#include <span>
#include <vector>

#include "../model/settings.h"

namespace bettertabs {

inline constexpr std::size_t no_index = static_cast<std::size_t>(-1);

struct Span {
    int start{0};
    int length{0};
    [[nodiscard]] int end() const noexcept { return start + length; }
    [[nodiscard]] bool operator==(const Span&) const = default;
};

struct StripLayoutInput {
    //! Available length along the strip.
    int length{0};
    TabSizing sizing{TabSizing::fit};
    TabAlign align{TabAlign::start};
    int spacing{0};
    //! Length of the overflow chevron, used only when the tabs do not fit.
    int chevron{0};
    //! Natural length of each tab: text plus padding.
    std::span<const int> extents;
    //! Kept on screen when the tabs overflow. no_index = none.
    std::size_t active{no_index};
};

struct StripLayout {
    //! One per input tab. Tabs outside [first, last) have length 0 and are not drawn.
    std::vector<Span> tabs;
    std::size_t first{0};
    std::size_t last{0};
    bool overflow{false};
    Span chevron;
};

//! Reuses `out`'s storage.
void layout_strip(const StripLayoutInput& in, StripLayout& out);

//! Index of the tab at `pos` along the strip, or no_index. Binary search on the placed tabs.
[[nodiscard]] std::size_t hit_test_strip(const StripLayout& layout, int pos) noexcept;

} // namespace bettertabs
