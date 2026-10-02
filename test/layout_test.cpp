// Offline tests for src/strip/strip_layout.cpp. Run build_tests.bat.

#include <cstdio>
#include <vector>

#include "../src/strip/strip_layout.h"

using namespace bettertabs;

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

StripLayout run(int length, TabSizing sizing, TabAlign align, const std::vector<int>& extents,
                std::size_t active = no_index, int spacing = 2, int chevron = 24) {
    StripLayoutInput in;
    in.length = length;
    in.sizing = sizing;
    in.align = align;
    in.spacing = spacing;
    in.chevron = chevron;
    in.extents = extents;
    in.active = active;
    StripLayout out;
    layout_strip(in, out);
    return out;
}

} // namespace

int main() {
    const std::vector<int> three = {50, 80, 60};
    {
        const StripLayout l = run(400, TabSizing::fit, TabAlign::start, three);
        check(!l.overflow && l.first == 0 && l.last == 3, "fit: all visible");
        check(l.tabs[0] == Span{0, 50} && l.tabs[1] == Span{52, 80} && l.tabs[2] == Span{134, 60}, "fit: positions");
    }
    {
        const StripLayout l = run(400, TabSizing::fit, TabAlign::end, three);
        check(l.tabs[2].end() == 400, "end alignment touches the far edge");
        const StripLayout c = run(400, TabSizing::fit, TabAlign::centre, three);
        check(c.tabs[0].start == (400 - 194) / 2, "centre alignment");
    }
    {
        const StripLayout l = run(400, TabSizing::equal, TabAlign::start, three);
        check(l.tabs[0].length == 80 && l.tabs[1].length == 80 && l.tabs[2].length == 80, "equal: widest wins");
    }
    {
        const StripLayout l = run(401, TabSizing::fill, TabAlign::start, three);
        int total = 0;
        for (const Span& s : l.tabs) total += s.length;
        check(total + 4 == 401 && l.tabs[2].end() == 401, "fill: exactly fills, remainder distributed");
    }
    {
        // Overflow keeps the active tab visible and places the chevron at the end.
        std::vector<int> many(20, 60);
        const StripLayout l = run(300, TabSizing::fit, TabAlign::start, many, 15);
        check(l.overflow, "overflow detected");
        check(l.first <= 15 && 15 < l.last, "active stays visible");
        check(l.chevron == Span{276, 24}, "chevron at the end");
        bool inside = true;
        for (std::size_t i = l.first; i < l.last; ++i) inside = inside && l.tabs[i].end() <= 276 - 2;
        check(inside, "visible tabs clear the chevron");
        bool hidden = true;
        for (std::size_t i = 0; i < many.size(); ++i) {
            if (i < l.first || i >= l.last) hidden = hidden && l.tabs[i].length == 0;
        }
        check(hidden, "off-window tabs have no length");
        check(hit_test_strip(l, l.tabs[15].start + 1) == 15, "hit test finds the active tab");
        check(hit_test_strip(l, 290) == no_index, "hit test on the chevron is no tab");
    }
    {
        // The chevron can lead instead: tabs start after it and still keep the active one.
        std::vector<int> many(20, 60);
        StripLayoutInput in;
        in.length = 300;
        in.spacing = 2;
        in.chevron = 24;
        in.chevron_position = ChevronPosition::start;
        in.extents = many;
        in.active = 15;
        StripLayout l;
        layout_strip(in, l);
        check(l.overflow && l.first <= 15 && 15 < l.last, "chevron at start: active stays visible");
        check(l.chevron == Span{0, 24}, "chevron at the start");
        bool inside = true;
        for (std::size_t i = l.first; i < l.last; ++i) {
            inside = inside && l.tabs[i].start >= 26 && l.tabs[i].end() <= 300;
        }
        check(inside, "chevron at start: tabs clear it and the far edge");
        check(l.tabs[l.first].start == 26, "chevron at start: first tab right after it");
        check(hit_test_strip(l, 10) == no_index, "chevron at start: hit test on it is no tab");
        check(hit_test_strip(l, l.tabs[15].start + 1) == 15, "chevron at start: hit test finds the active tab");
    }
    {
        const StripLayout l = run(30, TabSizing::fit, TabAlign::start, {500});
        check(l.first == 0 && l.last == 1 && l.tabs[0].length > 0 && l.tabs[0].end() <= 30, "single huge tab clipped");
    }
    {
        const StripLayout l = run(300, TabSizing::fit, TabAlign::start, three);
        check(hit_test_strip(l, 51) == no_index, "hit test in the gap is no tab");
        check(hit_test_strip(l, 0) == 0 && hit_test_strip(l, 131) == 1 && hit_test_strip(l, 133) == no_index &&
                  hit_test_strip(l, 134) == 2 && hit_test_strip(l, 193) == 2,
              "hit test edges");
        check(hit_test_strip(l, 194) == no_index && hit_test_strip(l, -1) == no_index, "hit test outside");
    }
    {
        const StripLayout l = run(0, TabSizing::fit, TabAlign::start, three);
        check(l.first == 0 && l.last == 0 && l.tabs.size() == 3, "zero length: nothing placed");
        const StripLayout e = run(300, TabSizing::fit, TabAlign::start, {});
        check(e.tabs.empty() && hit_test_strip(e, 5) == no_index, "no tabs");
    }
    {
        // Shrink before overflow: the long tab gives up length, the short ones keep theirs.
        const std::vector<int> tabs = {360, 100, 120, 90};
        StripLayoutInput in;
        in.length = 500;
        in.spacing = 2;
        in.chevron = 24;
        in.extents = tabs;
        in.shrink_floor = 60;
        StripLayout l;
        layout_strip(in, l);
        check(!l.overflow && l.first == 0 && l.last == 4, "shrink: all tabs stay, no chevron");
        check(l.tabs[1].length == 100 && l.tabs[2].length == 120 && l.tabs[3].length == 90, "shrink: short tabs untouched");
        check(l.tabs[0].length == 500 - 6 - 310 && l.tabs[3].end() == 500, "shrink: long tab takes exactly the rest");

        // Equal sizing shrinks every tab alike.
        in.sizing = TabSizing::equal;
        layout_strip(in, l);
        check(!l.overflow && l.tabs[0].length == l.tabs[3].length && l.tabs[3].end() <= 500, "shrink: equal stays equal");

        // Below the floor it overflows as before.
        in.sizing = TabSizing::fit;
        in.length = 200;
        layout_strip(in, l);
        check(l.overflow, "shrink: overflow once the floor does not fit");

        // A floor of 0 keeps the old behaviour.
        in.length = 500;
        in.shrink_floor = 0;
        layout_strip(in, l);
        check(l.overflow, "shrink: off without a floor");
    }
    {
        // Reusing the output does not reallocate once grown.
        std::vector<int> many(20, 60);
        StripLayoutInput in;
        in.length = 300;
        in.extents = many;
        StripLayout out;
        layout_strip(in, out);
        const Span* before = out.tabs.data();
        in.length = 5000;
        layout_strip(in, out);
        check(out.tabs.data() == before, "output storage reused");
    }

    std::printf("%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
