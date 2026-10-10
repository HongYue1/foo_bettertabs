#include "codec.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace bettertabs {

void clamp(Settings& s) noexcept {
    const auto limit = [](std::uint16_t& value, std::uint16_t lo, std::uint16_t hi) {
        value = std::clamp(value, lo, hi);
    };
    limit(s.pad_x, 0, 64);
    limit(s.pad_y, 0, 64);
    limit(s.spacing, 0, 64);
    limit(s.thickness, 0, 200);
    limit(s.corner_radius, 0, 32);
    limit(s.max_tab_width, 0, 2000);
    limit(s.animation_ms, 50, 1000);
    limit(s.switch_ms, 50, 1000);
    limit(s.hot_zone, 1, 32);
    limit(s.reveal_delay_ms, 0, 5000);
    limit(s.hide_delay_ms, 0, 5000);
    limit(s.linger_ms, 0, 5000);
    if (s.accent_strength != 0) s.accent_strength = std::clamp<std::uint8_t>(s.accent_strength, 5, 100);
    s.tint_strength = std::clamp<std::uint8_t>(s.tint_strength, 2, 60);
    s.line_width = (std::min)(s.line_width, std::uint8_t{8});
    s.transparent_opacity = (std::min)(s.transparent_opacity, std::uint8_t{100});
    if (s.hover_fill_strength != 0) s.hover_fill_strength = std::clamp<std::uint8_t>(s.hover_fill_strength, 2, 80);
    s.hover_line_width = (std::min)(s.hover_line_width, std::uint8_t{8});
    if (s.hover_line_opacity != 0) s.hover_line_opacity = std::clamp<std::uint8_t>(s.hover_line_opacity, 10, 100);
    limit(s.hover_fade_ms, 50, 1000);
    if (s.hover_style == HoverStyle::plain) s.hover_style = HoverStyle::fill;
    if (s.active_hover_fill_strength != 0) s.active_hover_fill_strength = std::clamp<std::uint8_t>(s.active_hover_fill_strength, 2, 80);
    s.active_hover_line_width = (std::min)(s.active_hover_line_width, std::uint8_t{8});
    if (s.active_hover_line_opacity != 0) s.active_hover_line_opacity = std::clamp<std::uint8_t>(s.active_hover_line_opacity, 10, 100);
    if (s.font.tenths_pt != 0) limit(s.font.tenths_pt, 40, 720);
    limit(s.font.weight, 0, 1000);
}

namespace {

// Byte IO ------------------------------------------------------------------------------------

class Writer {
public:
    explicit Writer(Bytes& out) noexcept : out_(out) {}

    void u8(std::uint8_t v) { out_.push_back(v); }
    void u16(std::uint16_t v) {
        u8(static_cast<std::uint8_t>(v));
        u8(static_cast<std::uint8_t>(v >> 8));
    }
    void u32(std::uint32_t v) {
        u16(static_cast<std::uint16_t>(v));
        u16(static_cast<std::uint16_t>(v >> 16));
    }
    void bytes(std::span<const std::uint8_t> data) { out_.insert(out_.end(), data.begin(), data.end()); }
    void guid(const GUID& g) {
        u32(static_cast<std::uint32_t>(g.Data1));
        u16(g.Data2);
        u16(g.Data3);
        bytes(std::span<const std::uint8_t>(g.Data4, 8));
    }

    [[nodiscard]] std::size_t size() const noexcept { return out_.size(); }

    void patch_u16(std::size_t at, std::uint16_t v) noexcept {
        out_[at] = static_cast<std::uint8_t>(v);
        out_[at + 1] = static_cast<std::uint8_t>(v >> 8);
    }
    void patch_u32(std::size_t at, std::uint32_t v) noexcept {
        patch_u16(at, static_cast<std::uint16_t>(v));
        patch_u16(at + 2, static_cast<std::uint16_t>(v >> 16));
    }

private:
    Bytes& out_;
};

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> data) noexcept : data_(data) {}

    [[nodiscard]] std::size_t remaining() const noexcept { return data_.size() - pos_; }

    bool u8(std::uint8_t& v) noexcept {
        if (remaining() < 1) return false;
        v = data_[pos_++];
        return true;
    }
    bool u16(std::uint16_t& v) noexcept {
        if (remaining() < 2) return false;
        v = static_cast<std::uint16_t>(data_[pos_] | (data_[pos_ + 1] << 8));
        pos_ += 2;
        return true;
    }
    bool u32(std::uint32_t& v) noexcept {
        std::uint16_t lo = 0;
        std::uint16_t hi = 0;
        if (remaining() < 4) return false;
        (void)u16(lo);
        (void)u16(hi);
        v = static_cast<std::uint32_t>(lo) | (static_cast<std::uint32_t>(hi) << 16);
        return true;
    }
    bool take(std::size_t n, std::span<const std::uint8_t>& out) noexcept {
        if (remaining() < n) return false;
        out = data_.subspan(pos_, n);
        pos_ += n;
        return true;
    }
    bool guid(GUID& g) noexcept {
        std::uint32_t d1 = 0;
        std::span<const std::uint8_t> d4;
        if (remaining() < 16) return false;
        (void)u32(d1);
        g.Data1 = d1;
        (void)u16(g.Data2);
        (void)u16(g.Data3);
        (void)take(8, d4);
        std::memcpy(g.Data4, d4.data(), 8);
        return true;
    }

private:
    std::span<const std::uint8_t> data_;
    std::size_t pos_{0};
};

// Fields -------------------------------------------------------------------------------------
//
// A field is (uint16 id, uint16 length, bytes). Readers take the value only when the length is
// at least the width they expect, so a later build may widen a field without breaking this one.

void field_u8(Writer& w, std::uint16_t id, std::uint8_t v) {
    w.u16(id);
    w.u16(1);
    w.u8(v);
}
void field_u16(Writer& w, std::uint16_t id, std::uint16_t v) {
    w.u16(id);
    w.u16(2);
    w.u16(v);
}
void field_u32(Writer& w, std::uint16_t id, std::uint32_t v) {
    w.u16(id);
    w.u16(4);
    w.u32(v);
}
void field_bytes(Writer& w, std::uint16_t id, std::span<const std::uint8_t> v) {
    const std::size_t n = (std::min)(v.size(), std::size_t{0xFFFF});
    w.u16(id);
    w.u16(static_cast<std::uint16_t>(n));
    w.bytes(v.first(n));
}
void field_string(Writer& w, std::uint16_t id, const std::string& v) {
    field_bytes(w, id,
                std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(v.data()), v.size()));
}

[[nodiscard]] std::uint32_t read_uint(std::span<const std::uint8_t> v) noexcept {
    std::uint32_t out = 0;
    for (std::size_t i = 0; i < v.size() && i < 4; ++i) out |= static_cast<std::uint32_t>(v[i]) << (8 * i);
    return out;
}

template <class E>
void read_enum(std::span<const std::uint8_t> v, E& out, E last) noexcept {
    if (v.empty()) return;
    if (v[0] <= static_cast<std::uint8_t>(last)) out = static_cast<E>(v[0]);
}
void read_bool(std::span<const std::uint8_t> v, bool& out) noexcept {
    if (!v.empty()) out = v[0] != 0;
}
void read_u8(std::span<const std::uint8_t> v, std::uint8_t& out) noexcept {
    if (!v.empty()) out = v[0];
}
void read_u16(std::span<const std::uint8_t> v, std::uint16_t& out) noexcept {
    if (v.size() >= 2) out = static_cast<std::uint16_t>(read_uint(v.first(2)));
}
void read_u32(std::span<const std::uint8_t> v, std::uint32_t& out) noexcept {
    if (v.size() >= 4) out = read_uint(v.first(4));
}

// Settings field ids. Frozen: never reuse or renumber a shipped id.
enum SettingId : std::uint16_t {
    s_position = 1,
    s_side_text = 2,
    s_sizing = 3,
    s_align = 4,
    s_pad_x = 5,
    s_pad_y = 6,
    s_spacing = 7,
    s_thickness = 8,
    s_visibility = 9,
    s_indicator = 10,
    s_accent_source = 11,
    s_accent_argb = 12,
    s_corner_radius = 13,
    s_chip = 14,
    s_animations = 15,
    s_animation_ms = 16,
    s_wheel = 17,
    s_middle_click = 18,
    s_drag = 19,
    s_lazy = 20,
    s_remember = 21,
    s_follow_play = 22,
    s_follow_stop = 23,
    s_hot_zone = 24,
    s_reveal_delay = 25,
    s_hide_delay = 26,
    s_linger = 27,
    s_reveal_mode = 28,
    s_show_hide_animation = 29,
    s_accent_strength = 30,
    s_strip_background = 31,
    s_background_argb = 32,
    s_tint_strength = 33,
    s_icons_only = 34,
    s_ctrl_tab = 35,
    s_max_tab_width = 36,
    s_switch_ms = 37,
    s_chevron_position = 38,
    s_shrink_titles = 39,
    s_line_width = 40,
    s_font_family = 41,
    s_font_size = 42,
    s_font_weight = 43,
    s_font_italic = 44,
    s_fallback1 = 45,
    s_fallback2 = 46,
    s_fallback3 = 47,
    s_hover_style = 48,
    s_hover_colour = 49,
    s_hover_argb = 50,
    s_hover_fill_strength = 51,
    s_hover_line_width = 52,
    s_hover_line_opacity = 53,
    s_hover_text = 54,
    s_hover_fade = 55,
    s_hover_fade_ms = 56,
    s_transparent_background = 57,
    s_active_hover_style = 58,
    s_active_hover_colour = 59,
    s_active_hover_argb = 60,
    s_active_hover_fill_strength = 61,
    s_active_hover_line_width = 62,
    s_active_hover_line_opacity = 63,
    s_active_hover_lighten = 64,
    s_active_hover_text = 65,
    s_hover_text_argb = 66,
    s_active_hover_text_argb = 67,
    s_custom_text = 68,
    s_text_argb = 69,
    s_custom_active_text = 70,
    s_active_text_argb = 71,
    s_transparent_opacity = 72,
};

void write_settings(Writer& w, const Settings& s) {
    field_u8(w, s_position, static_cast<std::uint8_t>(s.position));
    field_u8(w, s_side_text, static_cast<std::uint8_t>(s.side_text));
    field_u8(w, s_sizing, static_cast<std::uint8_t>(s.sizing));
    field_u8(w, s_align, static_cast<std::uint8_t>(s.align));
    field_u16(w, s_pad_x, s.pad_x);
    field_u16(w, s_pad_y, s.pad_y);
    field_u16(w, s_spacing, s.spacing);
    field_u16(w, s_thickness, s.thickness);
    field_u8(w, s_visibility, static_cast<std::uint8_t>(s.visibility));
    field_u8(w, s_indicator, static_cast<std::uint8_t>(s.indicator));
    field_u8(w, s_accent_source, static_cast<std::uint8_t>(s.accent_source));
    field_u32(w, s_accent_argb, s.accent_argb);
    field_u16(w, s_corner_radius, s.corner_radius);
    field_u8(w, s_chip, s.chip ? 1 : 0);
    field_u8(w, s_animations, s.animations ? 1 : 0);
    field_u16(w, s_animation_ms, s.animation_ms);
    field_u8(w, s_wheel, s.wheel_cycles ? 1 : 0);
    field_u8(w, s_middle_click, static_cast<std::uint8_t>(s.middle_click));
    field_u8(w, s_drag, s.drag_reorder ? 1 : 0);
    field_u8(w, s_lazy, s.lazy_children ? 1 : 0);
    field_u8(w, s_remember, s.remember_active ? 1 : 0);
    field_u16(w, s_follow_play, s.follow_play_tab);
    field_u16(w, s_follow_stop, s.follow_stop_tab);
    field_u16(w, s_hot_zone, s.hot_zone);
    field_u16(w, s_reveal_delay, s.reveal_delay_ms);
    field_u16(w, s_hide_delay, s.hide_delay_ms);
    field_u16(w, s_linger, s.linger_ms);
    field_u8(w, s_reveal_mode, static_cast<std::uint8_t>(s.reveal_mode));
    field_u8(w, s_show_hide_animation, static_cast<std::uint8_t>(s.show_hide_animation));
    field_u8(w, s_accent_strength, s.accent_strength);
    field_u8(w, s_strip_background, static_cast<std::uint8_t>(s.strip_background));
    field_u32(w, s_background_argb, s.background_argb);
    field_u8(w, s_tint_strength, s.tint_strength);
    field_u8(w, s_icons_only, s.icons_only ? 1 : 0);
    field_u8(w, s_ctrl_tab, s.ctrl_tab ? 1 : 0);
    field_u16(w, s_max_tab_width, s.max_tab_width);
    field_u16(w, s_switch_ms, s.switch_ms);
    field_u8(w, s_chevron_position, static_cast<std::uint8_t>(s.chevron_position));
    field_u8(w, s_shrink_titles, s.shrink_titles ? 1 : 0);
    field_u8(w, s_line_width, s.line_width);
    field_string(w, s_font_family, s.font.family);
    field_u16(w, s_font_size, s.font.tenths_pt);
    field_u16(w, s_font_weight, s.font.weight);
    field_u8(w, s_font_italic, s.font.italic ? 1 : 0);
    field_string(w, s_fallback1, s.font.fallbacks[0]);
    field_string(w, s_fallback2, s.font.fallbacks[1]);
    field_string(w, s_fallback3, s.font.fallbacks[2]);
    field_u8(w, s_hover_style, static_cast<std::uint8_t>(s.hover_style));
    field_u8(w, s_hover_colour, static_cast<std::uint8_t>(s.hover_colour));
    field_u32(w, s_hover_argb, s.hover_argb);
    field_u8(w, s_hover_fill_strength, s.hover_fill_strength);
    field_u8(w, s_hover_line_width, s.hover_line_width);
    field_u8(w, s_hover_line_opacity, s.hover_line_opacity);
    field_u8(w, s_hover_text, static_cast<std::uint8_t>(s.hover_text));
    field_u8(w, s_hover_fade, s.hover_fade ? 1 : 0);
    field_u16(w, s_hover_fade_ms, s.hover_fade_ms);
    field_u8(w, s_transparent_background, s.transparent_background ? 1 : 0);
    field_u8(w, s_active_hover_style, static_cast<std::uint8_t>(s.active_hover_style));
    field_u8(w, s_active_hover_colour, static_cast<std::uint8_t>(s.active_hover_colour));
    field_u32(w, s_active_hover_argb, s.active_hover_argb);
    field_u8(w, s_active_hover_fill_strength, s.active_hover_fill_strength);
    field_u8(w, s_active_hover_line_width, s.active_hover_line_width);
    field_u8(w, s_active_hover_line_opacity, s.active_hover_line_opacity);
    // What builds up to 0.9 read; s_active_hover_text comes later and wins.
    field_u8(w, s_active_hover_lighten, s.active_hover_text == HoverText::brighten ? 1 : 0);
    field_u8(w, s_active_hover_text, static_cast<std::uint8_t>(s.active_hover_text));
    field_u32(w, s_hover_text_argb, s.hover_text_argb);
    field_u32(w, s_active_hover_text_argb, s.active_hover_text_argb);
    field_u8(w, s_custom_text, s.custom_text ? 1 : 0);
    field_u32(w, s_text_argb, s.text_argb);
    field_u8(w, s_custom_active_text, s.custom_active_text ? 1 : 0);
    field_u32(w, s_active_text_argb, s.active_text_argb);
    field_u8(w, s_transparent_opacity, s.transparent_opacity);
}

//! Returns false for an id this build does not know.
bool read_setting(Settings& s, std::uint16_t id, std::span<const std::uint8_t> v) noexcept {
    switch (id) {
    case s_position: read_enum(v, s.position, StripPosition::right); return true;
    case s_side_text: read_enum(v, s.side_text, SideText::rotated); return true;
    case s_sizing: read_enum(v, s.sizing, TabSizing::fill); return true;
    case s_align: read_enum(v, s.align, TabAlign::end); return true;
    case s_pad_x: read_u16(v, s.pad_x); return true;
    case s_pad_y: read_u16(v, s.pad_y); return true;
    case s_spacing: read_u16(v, s.spacing); return true;
    case s_thickness: read_u16(v, s.thickness); return true;
    case s_visibility: read_enum(v, s.visibility, StripVisibility::auto_hide); return true;
    case s_indicator: read_enum(v, s.indicator, Indicator::tab_outline); return true;
    case s_accent_source: read_enum(v, s.accent_source, AccentSource::highlight); return true;
    case s_accent_argb: read_u32(v, s.accent_argb); return true;
    case s_corner_radius: read_u16(v, s.corner_radius); return true;
    case s_chip: read_bool(v, s.chip); return true;
    case s_animations: read_bool(v, s.animations); return true;
    case s_animation_ms: read_u16(v, s.animation_ms); return true;
    case s_wheel: read_bool(v, s.wheel_cycles); return true;
    case s_middle_click: read_enum(v, s.middle_click, MiddleClick::remove_tab); return true;
    case s_drag: read_bool(v, s.drag_reorder); return true;
    case s_lazy: read_bool(v, s.lazy_children); return true;
    case s_remember: read_bool(v, s.remember_active); return true;
    case s_follow_play: read_u16(v, s.follow_play_tab); return true;
    case s_follow_stop: read_u16(v, s.follow_stop_tab); return true;
    case s_hot_zone: read_u16(v, s.hot_zone); return true;
    case s_reveal_delay: read_u16(v, s.reveal_delay_ms); return true;
    case s_hide_delay: read_u16(v, s.hide_delay_ms); return true;
    case s_linger: read_u16(v, s.linger_ms); return true;
    case s_reveal_mode: read_enum(v, s.reveal_mode, RevealMode::push); return true;
    case s_show_hide_animation:
        read_enum(v, s.show_hide_animation, ShowHideAnimation::fade);
        return true;
    case s_accent_strength: read_u8(v, s.accent_strength); return true;
    case s_strip_background: read_enum(v, s.strip_background, StripBackground::accent_tint); return true;
    case s_background_argb: read_u32(v, s.background_argb); return true;
    case s_tint_strength: read_u8(v, s.tint_strength); return true;
    case s_icons_only: read_bool(v, s.icons_only); return true;
    case s_ctrl_tab: read_bool(v, s.ctrl_tab); return true;
    case s_max_tab_width: read_u16(v, s.max_tab_width); return true;
    case s_switch_ms: read_u16(v, s.switch_ms); return true;
    case s_chevron_position: read_enum(v, s.chevron_position, ChevronPosition::start); return true;
    case s_shrink_titles: read_bool(v, s.shrink_titles); return true;
    case s_line_width: read_u8(v, s.line_width); return true;
    case s_font_family: s.font.family.assign(reinterpret_cast<const char*>(v.data()), v.size()); return true;
    case s_font_size: read_u16(v, s.font.tenths_pt); return true;
    case s_font_weight: read_u16(v, s.font.weight); return true;
    case s_font_italic: read_bool(v, s.font.italic); return true;
    case s_fallback1: s.font.fallbacks[0].assign(reinterpret_cast<const char*>(v.data()), v.size()); return true;
    case s_fallback2: s.font.fallbacks[1].assign(reinterpret_cast<const char*>(v.data()), v.size()); return true;
    case s_fallback3: s.font.fallbacks[2].assign(reinterpret_cast<const char*>(v.data()), v.size()); return true;
    case s_hover_style: read_enum(v, s.hover_style, HoverStyle::none); return true;
    case s_hover_colour: read_enum(v, s.hover_colour, HoverColour::custom); return true;
    case s_hover_argb: read_u32(v, s.hover_argb); return true;
    case s_hover_fill_strength: read_u8(v, s.hover_fill_strength); return true;
    case s_hover_line_width: read_u8(v, s.hover_line_width); return true;
    case s_hover_line_opacity: read_u8(v, s.hover_line_opacity); return true;
    case s_hover_text: read_enum(v, s.hover_text, HoverText::custom); return true;
    case s_hover_fade: read_bool(v, s.hover_fade); return true;
    case s_hover_fade_ms: read_u16(v, s.hover_fade_ms); return true;
    case s_transparent_background: read_bool(v, s.transparent_background); return true;
    case s_active_hover_style: read_enum(v, s.active_hover_style, HoverStyle::plain); return true;
    case s_active_hover_colour: read_enum(v, s.active_hover_colour, HoverColour::custom); return true;
    case s_active_hover_argb: read_u32(v, s.active_hover_argb); return true;
    case s_active_hover_fill_strength: read_u8(v, s.active_hover_fill_strength); return true;
    case s_active_hover_line_width: read_u8(v, s.active_hover_line_width); return true;
    case s_active_hover_line_opacity: read_u8(v, s.active_hover_line_opacity); return true;
    case s_active_hover_lighten: {
        bool lighten = false;
        read_bool(v, lighten);
        s.active_hover_text = lighten ? HoverText::brighten : HoverText::unchanged;
        return true;
    }
    case s_active_hover_text: read_enum(v, s.active_hover_text, HoverText::custom); return true;
    case s_hover_text_argb: read_u32(v, s.hover_text_argb); return true;
    case s_active_hover_text_argb: read_u32(v, s.active_hover_text_argb); return true;
    case s_custom_text: read_bool(v, s.custom_text); return true;
    case s_text_argb: read_u32(v, s.text_argb); return true;
    case s_custom_active_text: read_bool(v, s.custom_active_text); return true;
    case s_active_text_argb: read_u32(v, s.active_text_argb); return true;
    case s_transparent_opacity: read_u8(v, s.transparent_opacity); return true;
    default: return false;
    }
}

//! Reads (id, length, bytes) fields until the input ends or a field runs past it.
template <class F>
void read_fields(Reader& r, F&& on_field) {
    while (r.remaining() >= 4) {
        std::uint16_t id = 0;
        std::uint16_t length = 0;
        std::span<const std::uint8_t> value;
        (void)r.u16(id);
        (void)r.u16(length);
        if (!r.take(length, value)) return;
        on_field(id, value);
    }
}

void write_raw_fields(Writer& w, const std::vector<RawField>& fields) {
    for (const RawField& f : fields) field_bytes(w, f.id, f.data);
}

enum SectionTag : std::uint16_t { sec_settings = 1, sec_children = 2, sec_state = 3 };

constexpr std::uint8_t magic[4] = {'B', 'T', 'A', 'B'};
constexpr std::uint16_t min_reader_version = 1;

//! Starts a section and returns the offset of its length, for patch_u32 once it is written.
std::size_t begin_section(Writer& w, std::uint16_t tag) {
    w.u16(tag);
    const std::size_t at = w.size();
    w.u32(0);
    return at;
}
void end_section(Writer& w, std::size_t length_at) {
    w.patch_u32(length_at, static_cast<std::uint32_t>(w.size() - length_at - 4));
}

enum ExtraId : std::uint16_t {
    e_title = 1,
    e_title_is_format = 2,
    e_hidden = 3,
    e_icon = 4,
    e_icon_font = 5,
    e_use_custom_title = 6,
    e_show_on_play = 7,
    e_show_on_stop = 8,
};

} // namespace

Bytes encode_instance(const InstanceData& data) {
    Bytes out;
    out.reserve(64 + data.children.size() * 64);
    Writer w(out);
    w.bytes(magic);
    w.u16(instance_format_version);
    w.u16(min_reader_version);

    std::size_t at = begin_section(w, sec_settings);
    write_settings(w, data.settings);
    write_raw_fields(w, data.unknown_settings);
    end_section(w, at);

    at = begin_section(w, sec_children);
    w.u32(static_cast<std::uint32_t>(data.children.size()));
    for (const ChildRecord& child : data.children) {
        w.guid(child.guid);
        w.u32(static_cast<std::uint32_t>(child.config.size()));
        w.bytes(child.config);
        w.u32(static_cast<std::uint32_t>(child.extra.size()));
        w.bytes(child.extra);
    }
    end_section(w, at);

    at = begin_section(w, sec_state);
    w.u32(data.active);
    end_section(w, at);

    for (const RawField& section : data.unknown_sections) {
        at = begin_section(w, section.id);
        w.bytes(section.data);
        end_section(w, at);
    }
    return out;
}

InstanceData decode_instance(std::span<const std::uint8_t> bytes) {
    InstanceData data;
    Reader r(bytes);
    std::span<const std::uint8_t> head;
    if (!r.take(4, head) || std::memcmp(head.data(), magic, 4) != 0) return data;
    std::uint16_t version = 0;
    std::uint16_t min_reader = 0;
    if (!r.u16(version) || !r.u16(min_reader)) return data;
    // A blob that declares itself unreadable for us keeps default settings, but its children
    // are still ours to show: they have their own, stable section.
    const bool settings_readable = min_reader <= instance_format_version;

    while (r.remaining() >= 6) {
        std::uint16_t tag = 0;
        std::uint32_t length = 0;
        std::span<const std::uint8_t> body;
        (void)r.u16(tag);
        (void)r.u32(length);
        if (!r.take(length, body)) break;
        Reader s(body);

        switch (tag) {
        case sec_settings:
            if (!settings_readable) break;
            read_fields(s, [&](std::uint16_t id, std::span<const std::uint8_t> v) {
                if (!read_setting(data.settings, id, v)) {
                    data.unknown_settings.push_back(RawField{id, Bytes(v.begin(), v.end())});
                }
            });
            clamp(data.settings);
            break;

        case sec_children: {
            std::uint32_t count = 0;
            if (!s.u32(count)) break;
            // A count larger than the bytes could hold is junk; the loop stops at the first
            // short read anyway, so no reserve() of an attacker-sized number.
            for (std::uint32_t i = 0; i < count; ++i) {
                ChildRecord child;
                std::uint32_t n = 0;
                std::span<const std::uint8_t> config;
                std::span<const std::uint8_t> extra;
                if (!s.guid(child.guid) || !s.u32(n) || !s.take(n, config)) break;
                if (!s.u32(n) || !s.take(n, extra)) break;
                child.config.assign(config.begin(), config.end());
                child.extra.assign(extra.begin(), extra.end());
                data.children.push_back(std::move(child));
            }
            break;
        }

        case sec_state:
            (void)s.u32(data.active);
            break;

        default:
            data.unknown_sections.push_back(RawField{tag, Bytes(body.begin(), body.end())});
            break;
        }
    }
    return data;
}

Bytes encode_tab_extra(const TabExtra& extra) {
    Bytes out;
    Writer w(out);
    w.u16(tab_extra_version);
    field_u8(w, e_use_custom_title, extra.use_custom_title ? 1 : 0);
    field_string(w, e_title, extra.title);
    field_u8(w, e_title_is_format, extra.title_is_format ? 1 : 0);
    field_u8(w, e_hidden, extra.hidden ? 1 : 0);
    field_u32(w, e_icon, extra.icon);
    field_string(w, e_icon_font, extra.icon_font);
    field_u8(w, e_show_on_play, extra.show_on_play ? 1 : 0);
    field_u8(w, e_show_on_stop, extra.show_on_stop ? 1 : 0);
    write_raw_fields(w, extra.unknown);
    return out;
}

TabExtra decode_tab_extra(std::span<const std::uint8_t> bytes) {
    TabExtra extra;
    Reader r(bytes);
    std::uint16_t version = 0;
    if (!r.u16(version)) return extra;
    read_fields(r, [&](std::uint16_t id, std::span<const std::uint8_t> v) {
        switch (id) {
        case e_use_custom_title: read_bool(v, extra.use_custom_title); break;
        case e_title: extra.title.assign(reinterpret_cast<const char*>(v.data()), v.size()); break;
        case e_title_is_format: read_bool(v, extra.title_is_format); break;
        case e_hidden: read_bool(v, extra.hidden); break;
        case e_icon: read_u32(v, extra.icon); break;
        case e_icon_font:
            extra.icon_font.assign(reinterpret_cast<const char*>(v.data()), v.size());
            break;
        case e_show_on_play: read_bool(v, extra.show_on_play); break;
        case e_show_on_stop: read_bool(v, extra.show_on_stop); break;
        default: extra.unknown.push_back(RawField{id, Bytes(v.begin(), v.end())}); break;
        }
    });
    return extra;
}

} // namespace bettertabs
