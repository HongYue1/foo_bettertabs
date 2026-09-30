#include "strip_layout.h"

#include <algorithm>

namespace bettertabs {

void layout_strip(const StripLayoutInput& in, StripLayout& out) {
    const std::size_t n = in.extents.size();
    out.tabs.assign(n, Span{});
    out.first = 0;
    out.last = 0;
    out.overflow = false;
    out.chevron = Span{};
    if (n == 0 || in.length <= 0) return;

    const int spacing = (std::max)(0, in.spacing);
    int widest = 0;
    for (const int e : in.extents) widest = (std::max)(widest, (std::max)(0, e));

    // Natural lengths before any stretching.
    for (std::size_t i = 0; i < n; ++i) {
        out.tabs[i].length = in.sizing == TabSizing::equal ? widest : (std::max)(0, in.extents[i]);
    }

    long long total = static_cast<long long>(spacing) * static_cast<long long>(n - 1);
    for (const Span& s : out.tabs) total += s.length;

    if (total <= in.length) {
        out.first = 0;
        out.last = n;
        int slack = in.length - static_cast<int>(total);
        if (in.sizing == TabSizing::fill && slack > 0) {
            // Share the slack; the remainder goes to the first tabs, one pixel each.
            const int each = slack / static_cast<int>(n);
            int extra = slack % static_cast<int>(n);
            for (Span& s : out.tabs) {
                s.length += each + (extra > 0 ? 1 : 0);
                if (extra > 0) --extra;
            }
            slack = 0;
        }
        int pos = 0;
        if (in.align == TabAlign::centre) pos = slack / 2;
        if (in.align == TabAlign::end) pos = slack;
        for (Span& s : out.tabs) {
            s.start = pos;
            pos += s.length + spacing;
        }
        return;
    }

    // Overflow: a chevron at the end and a window of whole tabs that contains the active one.
    out.overflow = true;
    const int chevron = std::clamp(in.chevron, 0, in.length);
    out.chevron = Span{in.length - chevron, chevron};
    const int avail = (std::max)(0, in.length - chevron - (chevron > 0 ? spacing : 0));

    const auto fits = [&](std::size_t first, std::size_t last) {
        long long used = 0;
        for (std::size_t i = first; i < last; ++i) {
            used += out.tabs[i].length + (i > first ? spacing : 0);
        }
        return used <= avail;
    };

    std::size_t first = 0;
    std::size_t last = 0;
    while (last < n && fits(first, last + 1)) ++last;
    if (in.active != no_index && in.active < n && in.active >= last) {
        last = in.active + 1;
        first = in.active;
        while (first > 0 && fits(first - 1, last)) --first;
        // Fill any room left after the active tab.
        while (last < n && fits(first, last + 1)) ++last;
    }
    if (last == first) last = first + 1; // One tab wider than everything: show it clipped.

    int pos = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (i < first || i >= last) {
            out.tabs[i] = Span{0, 0};
            continue;
        }
        out.tabs[i].start = pos;
        out.tabs[i].length = (std::min)(out.tabs[i].length, (std::max)(0, avail - pos));
        pos += out.tabs[i].length + spacing;
    }
    out.first = first;
    out.last = last;
}

std::size_t hit_test_strip(const StripLayout& layout, int pos) noexcept {
    std::size_t lo = layout.first;
    std::size_t hi = layout.last;
    // Placed tabs are sorted and non-overlapping: find the last one starting at or before pos.
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (layout.tabs[mid].start <= pos) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo == layout.first) return no_index;
    const std::size_t i = lo - 1;
    return pos < layout.tabs[i].end() ? i : no_index;
}

} // namespace bettertabs
