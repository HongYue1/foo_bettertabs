#pragma once

// Per-instance settings. Plain values with explicit defaults; no SDK, no Win32, so the codec and
// the offline tests can use it. Stored field by field (codec.cpp), never as a raw struct, so
// adding a field never invalidates an older layout and an older build can read a newer one.

#include <array>
#include <cstdint>
#include <string>

namespace bettertabs {

enum class StripPosition : std::uint8_t { top, bottom, left, right };
enum class SideText : std::uint8_t { horizontal, rotated };
enum class TabSizing : std::uint8_t { fit, equal, fill };
enum class TabAlign : std::uint8_t { start, centre, end };
//! Which end of the strip the overflow chevron sits at.
enum class ChevronPosition : std::uint8_t { end, start };
enum class StripVisibility : std::uint8_t { always, never, two_or_more, auto_hide };
//! tab: the fill runs to the strip's edge facing the panel, rounded on the far side only.
//! tab_outline: the same shape as a solid outline over a faint fill.
enum class Indicator : std::uint8_t { underline, pill, none, tab, tab_outline };
//! highlight: Default UI's highlight colour, Columns UI's active item frame.
enum class AccentSource : std::uint8_t { selection, custom, cover, highlight };
enum class StripBackground : std::uint8_t { theme, custom, accent_tint };
//! remove_tab is reserved (read as hide_tab): removing a panel by a stray click is too easy.
enum class MiddleClick : std::uint8_t { nothing, hide_tab, remove_tab };
enum class RevealMode : std::uint8_t { overlay, push };
enum class ShowHideAnimation : std::uint8_t { none, slide, fade };
//! How a hovered tab is marked (Settings::hover_style for the others, active_hover_style for the
//! active one). plain: the active tab's faint wash, only offered for it.
//! New values go at the end: the codec stores the number.
enum class HoverStyle : std::uint8_t { fill, outline, outline_fill, underline, underline_fill, none, plain };
//! The colour of the hover mark. text: the strip's text colour (a neutral wash).
enum class HoverColour : std::uint8_t { text, accent, custom };
//! What the hovered tab's title does: brighten (the other tabs: to the active tab's text colour;
//! the active tab: lighter, towards white), stay as it is, take the hover colour, or take its own
//! colour (Settings::hover_text_argb, active_hover_text_argb).
//! New values go at the end: the codec stores the number.
enum class HoverText : std::uint8_t { brighten, unchanged, colour, custom };
//! The colour of the chips (Settings::chip). neutral: the strip's text colour; accent: the accent
//! (whatever its source, the cover's colour too).
//! New values go at the end: the codec stores the number.
enum class ChipColour : std::uint8_t { neutral, accent, custom };

//! "No tab" for the follow-playback targets.
inline constexpr std::uint16_t no_tab = 0xFFFF;

//! The tab font. An empty family follows the host (Columns UI's font for this component, or the
//! Default UI's tab font). Fallback families are tried in order for characters the font lacks.
struct TabFont {
    std::string family;
    //! Tenths of a point; 0 = the host's size.
    std::uint16_t tenths_pt{0};
    //! 0 = regular.
    std::uint16_t weight{0};
    bool italic{false};
    std::array<std::string, 3> fallbacks;

    [[nodiscard]] bool operator==(const TabFont&) const = default;
};

//! Settings::accent_strength 0 (automatic): the pill and tab fill in percent on a dark and on a
//! light strip. The Look page's slider rests there while Automatic is ticked.
inline constexpr std::uint8_t auto_fill_dark = 50;
inline constexpr std::uint8_t auto_fill_light = 40;
//! Settings::chip_strength 0 (automatic): the chips' fill in percent on a dark and on a light
//! strip. The Look page's slider rests there while Automatic is ticked.
inline constexpr std::uint8_t auto_chip_dark = 18;
inline constexpr std::uint8_t auto_chip_light = 15;
//! Settings::hover_fill_strength and active_hover_fill_strength 0 (automatic): the hover fill
//! in percent on the other tabs and on the active tab. The Hover page's sliders rest there.
inline constexpr std::uint8_t auto_hover_fill = 18;
inline constexpr std::uint8_t auto_active_hover_fill = 23;

struct Settings {
    StripPosition position{StripPosition::top};
    //! Rotated since 0.3.1: horizontal text makes a side strip as wide as its longest title.
    SideText side_text{SideText::rotated};
    TabSizing sizing{TabSizing::fit};
    TabAlign align{TabAlign::start};
    ChevronPosition chevron_position{ChevronPosition::end};
    //! When the tabs do not fit, shorten the longest titles (ellipsis) before overflowing to
    //! the chevron. Off = overflow straight away and keep every visible title whole.
    bool shrink_titles{false};

    // Metrics, all in DIPs.
    std::uint16_t pad_x{12};
    std::uint16_t pad_y{6};
    std::uint16_t spacing{2};
    //! Strip thickness; 0 = from the font.
    std::uint16_t thickness{0};

    StripVisibility visibility{StripVisibility::always};
    Indicator indicator{Indicator::underline};
    //! Line of the underline or the outlined tab, in DIPs; 0 = automatic (2 and 1.5).
    std::uint8_t line_width{0};
    TabFont font;
    AccentSource accent_source{AccentSource::selection};
    //! 0xAARRGGBB, used when accent_source == custom.
    std::uint32_t accent_argb{0xFF3EA6FFu};
    //! Opacity of the active tab's accent fill (pill, chip) in percent; 0 = automatic.
    std::uint8_t accent_strength{0};
    StripBackground strip_background{StripBackground::theme};
    //! 0xAARRGGBB, used when strip_background == custom.
    std::uint32_t background_argb{0xFF202020u};
    //! How much accent goes into an accent-tinted strip, in percent.
    std::uint8_t tint_strength{12};
    //! Text colours, 0xAARRGGBB: the other tabs' titles (else the theme's text, dimmed) and the
    //! active and selected tabs' (else the theme's text). Used as chosen, without the contrast
    //! checks the theme's colours get.
    bool custom_text{false};
    std::uint32_t text_argb{0xFFA0A0A0u};
    bool custom_active_text{false};
    std::uint32_t active_text_argb{0xFFFFFFFFu};
    //! The strip shows what its host paints behind it (a Columns UI theme's background image)
    //! instead of its own background; tabs keep their fills. Not while auto-hide draws the strip
    //! over the panel: there is no layout background there, only the panel.
    bool transparent_background{false};
    //! With a transparent background: how much of the strip's own background is painted over
    //! what the host paints, in percent (0 = none, fully transparent).
    std::uint8_t transparent_opacity{0};
    std::uint16_t corner_radius{4};
    bool chip{false};
    ChipColour chip_colour{ChipColour::neutral};
    //! 0xAARRGGBB, used when chip_colour == custom.
    std::uint32_t chip_argb{0xFF3EA6FFu};
    //! Opacity of the chips' fill in percent; 0 = automatic (auto_chip_dark, auto_chip_light).
    std::uint8_t chip_strength{0};
    //! Tab switches animate: the indicator (underline, pill or chip fill) slides.
    bool animations{true};
    //! Length of the auto-hide show/hide animation.
    std::uint16_t animation_ms{150};
    //! Length of the tab switch animation.
    std::uint16_t switch_ms{150};

    // Hover (tabs other than the active one).
    HoverStyle hover_style{HoverStyle::fill};
    HoverColour hover_colour{HoverColour::text};
    //! 0xAARRGGBB, used when hover_colour == custom.
    std::uint32_t hover_argb{0xFF3EA6FFu};
    //! Opacity of the hover fill in percent; 0 = automatic.
    std::uint8_t hover_fill_strength{0};
    //! Outline or underline width in DIPs; 0 = automatic (1.5 and 2).
    std::uint8_t hover_line_width{0};
    //! Opacity of the outline or underline in percent; 0 = automatic.
    std::uint8_t hover_line_opacity{0};
    HoverText hover_text{HoverText::brighten};
    //! Used when hover_text == custom.
    std::uint32_t hover_text_argb{0xFFFFFFFFu};
    //! The hover mark fades in and out over hover_fade_ms.
    bool hover_fade{false};
    std::uint16_t hover_fade_ms{120};

    // Hover on the active tab (the fade above applies to it too). plain: a faint wash, as before
    // the active tab could have its own style.
    HoverStyle active_hover_style{HoverStyle::plain};
    HoverColour active_hover_colour{HoverColour::text};
    std::uint32_t active_hover_argb{0xFF3EA6FFu};
    std::uint8_t active_hover_fill_strength{0};
    std::uint8_t active_hover_line_width{0};
    std::uint8_t active_hover_line_opacity{0};
    //! brighten: the title gets lighter (OKLab lightness towards white); up to 0.9 a checkbox
    //! (the codec still writes it as s_active_hover_lighten).
    HoverText active_hover_text{HoverText::unchanged};
    std::uint32_t active_hover_text_argb{0xFFFFFFFFu};

    bool wheel_cycles{true};
    MiddleClick middle_click{MiddleClick::nothing};
    bool drag_reorder{true};
    //! Create a child's window on first activation (true) or all at once (false).
    bool lazy_children{true};
    bool remember_active{true};
    //! Unused since 0.3.0 (follow playback is per tab, TabExtra::show_on_play/stop); kept so
    //! the stored field round-trips.
    std::uint16_t follow_play_tab{no_tab};
    std::uint16_t follow_stop_tab{no_tab};
    //! Tabs with an icon show only the icon (the title becomes the tooltip).
    bool icons_only{false};
    //! Ctrl+Tab / Ctrl+Shift+Tab cycle the tabs while focus is inside the container.
    bool ctrl_tab{true};
    //! Longest title in DIPs before it is cut with an ellipsis (and shown whole as a tooltip);
    //! 0 = no limit.
    std::uint16_t max_tab_width{240};

    // Auto-hide.
    std::uint16_t hot_zone{6};
    std::uint16_t reveal_delay_ms{0};
    std::uint16_t hide_delay_ms{400};
    std::uint16_t linger_ms{700};
    RevealMode reveal_mode{RevealMode::overlay};
    ShowHideAnimation show_hide_animation{ShowHideAnimation::slide};

    [[nodiscard]] bool operator==(const Settings&) const = default;
};

//! Pulls every numeric field into its supported range. Enums are range-checked by the codec.
void clamp(Settings& settings) noexcept;

} // namespace bettertabs
