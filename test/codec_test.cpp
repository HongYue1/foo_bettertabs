// Offline tests for src/model/codec.cpp. No SDK, no window. Run build_tests.bat.

#include <cstdio>
#include <cstring>

#include "../src/model/codec.h"

using namespace bettertabs;

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

constexpr GUID guid_a = {0x11111111, 0x2222, 0x3333, {1, 2, 3, 4, 5, 6, 7, 8}};
constexpr GUID guid_b = {0xAABBCCDD, 0xEEFF, 0x0011, {9, 10, 11, 12, 13, 14, 15, 16}};

bool same_guid(const GUID& a, const GUID& b) { return std::memcmp(&a, &b, sizeof(GUID)) == 0; }

Settings odd_settings() {
    Settings s;
    s.position = StripPosition::left;
    s.side_text = SideText::horizontal;
    s.sizing = TabSizing::fill;
    s.align = TabAlign::centre;
    s.chevron_position = ChevronPosition::start;
    s.shrink_titles = true;
    s.pad_x = 20;
    s.pad_y = 3;
    s.spacing = 0;
    s.thickness = 40;
    s.visibility = StripVisibility::auto_hide;
    s.indicator = Indicator::tab_outline;
    s.line_width = 3;
    s.font.family = "IBM Plex Sans";
    s.font.tenths_pt = 105;
    s.font.weight = 600;
    s.font.italic = true;
    s.font.fallbacks = {"IBM Plex Sans JP", "", "Noto Sans Arabic"};
    s.accent_source = AccentSource::highlight;
    s.accent_argb = 0xFF102030u;
    s.corner_radius = 9;
    s.chip = true;
    s.animations = false;
    s.animation_ms = 220;
    s.wheel_cycles = false;
    s.middle_click = MiddleClick::hide_tab;
    s.drag_reorder = false;
    s.lazy_children = false;
    s.remember_active = false;
    s.follow_play_tab = 2;
    s.follow_stop_tab = 0;
    s.hot_zone = 8;
    s.reveal_delay_ms = 150;
    s.hide_delay_ms = 900;
    s.linger_ms = 1200;
    s.reveal_mode = RevealMode::push;
    s.show_hide_animation = ShowHideAnimation::fade;
    s.accent_strength = 60;
    s.strip_background = StripBackground::accent_tint;
    s.background_argb = 0xFF332211u;
    s.tint_strength = 20;
    s.icons_only = true;
    s.ctrl_tab = false;
    s.max_tab_width = 100;
    s.switch_ms = 300;
    s.hover_style = HoverStyle::outline_fill;
    s.hover_colour = HoverColour::custom;
    s.hover_argb = 0xFF405060u;
    s.hover_fill_strength = 25;
    s.hover_line_width = 3;
    s.hover_line_opacity = 70;
    s.hover_text = HoverText::colour;
    s.hover_fade = true;
    s.hover_fade_ms = 240;
    s.transparent_background = true;
    s.active_hover_style = HoverStyle::underline_fill;
    s.active_hover_colour = HoverColour::accent;
    s.active_hover_argb = 0xFF102030u;
    s.active_hover_fill_strength = 30;
    s.active_hover_line_width = 2;
    s.active_hover_line_opacity = 60;
    s.active_hover_lighten = true;
    return s;
}

InstanceData sample() {
    InstanceData d;
    d.settings = odd_settings();
    ChildRecord a;
    a.guid = guid_a;
    a.config = {1, 2, 3};
    TabExtra ea;
    ea.use_custom_title = true;
    ea.title = "Now playing \xE2\x99\xAA";
    ea.hidden = true;
    ea.icon = 0xE8D6;
    ea.icon_font = "Segoe MDL2 Assets";
    ea.show_on_play = true;
    a.extra = encode_tab_extra(ea);
    ChildRecord b;
    b.guid = guid_b;
    d.children = {a, b};
    d.active = 1;
    return d;
}

} // namespace

int main() {
    {
        const InstanceData def;
        const InstanceData back = decode_instance(encode_instance(def));
        check(back.settings == Settings{}, "defaults round-trip");
        check(back.children.empty() && back.active == 0, "empty children round-trip");
    }
    {
        const InstanceData d = sample();
        const InstanceData back = decode_instance(encode_instance(d));
        check(back.settings == d.settings, "every setting round-trips");
        check(back.children.size() == 2, "child count");
        check(same_guid(back.children[0].guid, guid_a) && same_guid(back.children[1].guid, guid_b), "child GUIDs");
        check(back.children[0].config == d.children[0].config && back.children[1].config.empty(), "child configs");
        check(back.active == 1, "active index");
        const TabExtra ea = decode_tab_extra(back.children[0].extra);
        check(ea.use_custom_title && ea.title == "Now playing \xE2\x99\xAA" && ea.hidden && ea.icon == 0xE8D6 &&
                  ea.icon_font == "Segoe MDL2 Assets" && !ea.title_is_format && ea.show_on_play &&
                      !ea.show_on_stop,
              "tab extra round-trips");
        const TabExtra eb = decode_tab_extra(back.children[1].extra);
        check(eb == TabExtra{}, "empty tab extra reads as defaults");
    }
    {
        // A newer build's unknown setting and section survive a load/save cycle.
        InstanceData d = sample();
        d.unknown_settings.push_back(RawField{900, {7, 7}});
        d.unknown_sections.push_back(RawField{77, {1, 2, 3, 4}});
        const InstanceData back = decode_instance(encode_instance(d));
        check(back.unknown_settings.size() == 1 && back.unknown_settings[0].id == 900 &&
                  back.unknown_settings[0].data == Bytes({7, 7}),
              "unknown setting kept");
        check(back.unknown_sections.size() == 1 && back.unknown_sections[0].id == 77 &&
                  back.unknown_sections[0].data == Bytes({1, 2, 3, 4}),
              "unknown section kept");
        check(back.settings == d.settings, "known settings unaffected by unknown ones");
        check(encode_instance(back) == encode_instance(d), "second save is byte-identical");
    }
    {
        // Every truncation reads without crashing and never invents children.
        const Bytes full = encode_instance(sample());
        bool ok = true;
        for (std::size_t n = 0; n < full.size(); ++n) {
            const InstanceData back = decode_instance(std::span<const std::uint8_t>(full.data(), n));
            if (back.children.size() > 2) ok = false;
        }
        check(ok, "all truncations decode safely");
    }
    {
        const Bytes junk = {'X', 'Y', 'Z', 'W', 1, 0, 1, 0, 1, 0, 255, 255, 255, 255};
        check(decode_instance(junk).settings == Settings{}, "bad magic -> defaults");
        check(decode_instance({}).children.empty(), "empty blob -> defaults");
        // Children section claiming 4 billion children with no bytes behind them.
        Bytes huge = {'B', 'T', 'A', 'B', 1, 0, 1, 0, 2, 0, 4, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF};
        check(decode_instance(huge).children.empty(), "absurd child count is harmless");
    }
    {
        // Out-of-range enum and numeric values are rejected or clamped.
        Bytes blob = {'B', 'T', 'A', 'B', 1, 0, 1, 0, 1, 0, 11, 0, 0, 0,
                      1, 0, 1, 0, 9,              // position = 9 (invalid)
                      5, 0, 2, 0, 0xFF, 0xFF};    // pad_x = 65535
        const InstanceData back = decode_instance(blob);
        check(back.settings.position == StripPosition::top, "invalid enum keeps default");
        check(back.settings.pad_x == 64, "numeric setting clamped");
    }
    {
        // A blob that says it needs a newer reader: settings default, children still load.
        InstanceData d = sample();
        Bytes blob = encode_instance(d);
        blob[6] = 9; // min_reader_version
        const InstanceData back = decode_instance(blob);
        check(back.settings == Settings{}, "unreadable settings -> defaults");
        check(back.children.size() == 2, "children survive a newer min reader");
    }
    {
        TabExtra e;
        e.unknown.push_back(RawField{50, {9}});
        const TabExtra back = decode_tab_extra(encode_tab_extra(e));
        check(back.unknown.size() == 1 && back.unknown[0].id == 50, "unknown tab extra field kept");
    }

    std::printf("%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
