#pragma once

// Colour maths in OKLab / OKLCh (Bjoern Ottosson, 2020): a perceptual space where equal steps
// look equal and lightness means what the eye sees. Ported from foo_mediabar (src/model/colour.h
// and the accent half of src/render/theme.h), which ported it from foo_onscreendisplay; the legibility step
// must stay identical so a cover gives the same accent in all three components.
//
// Header-only, no Windows dependency, so the offline accent test compiles it as is. Colours
// cross this header as 0x00RRGGBB (an alpha byte is ignored on the way in, zero on the way out).

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace bettertabs::colour {

struct Lab {
    float L{0.0f}; // 0 black .. 1 white
    float a{0.0f}; // green .. red
    float b{0.0f}; // blue .. yellow
};

[[nodiscard]] inline float srgb_to_linear(float c) noexcept {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

[[nodiscard]] inline float linear_to_srgb(float c) noexcept {
    return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

[[nodiscard]] inline Lab linear_to_oklab(float r, float g, float b) noexcept {
    const float l = std::cbrt(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
    const float m = std::cbrt(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
    const float s = std::cbrt(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
    return Lab{0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
               1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
               0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s};
}

inline void oklab_to_linear(const Lab& c, float& r, float& g, float& b) noexcept {
    const float l0 = c.L + 0.3963377774f * c.a + 0.2158037573f * c.b;
    const float m0 = c.L - 0.1055613458f * c.a - 0.0638541728f * c.b;
    const float s0 = c.L - 0.0894841775f * c.a - 1.2914855480f * c.b;
    const float l = l0 * l0 * l0;
    const float m = m0 * m0 * m0;
    const float s = s0 * s0 * s0;
    r = 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
    g = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
    b = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
}

[[nodiscard]] inline Lab from_rgb(std::uint32_t rgb) noexcept {
    return linear_to_oklab(srgb_to_linear(static_cast<float>((rgb >> 16) & 0xffu) / 255.0f),
                           srgb_to_linear(static_cast<float>((rgb >> 8) & 0xffu) / 255.0f),
                           srgb_to_linear(static_cast<float>(rgb & 0xffu) / 255.0f));
}

[[nodiscard]] inline bool in_gamut(const Lab& c) noexcept {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    oklab_to_linear(c, r, g, b);
    constexpr float e = 1e-4f;
    return r >= -e && r <= 1.0f + e && g >= -e && g <= 1.0f + e && b >= -e && b <= 1.0f + e;
}

//! Clamps to sRGB; callers that care about hue go through from_lch, which never needs to.
[[nodiscard]] inline std::uint32_t to_rgb(const Lab& c) noexcept {
    float lin[3]{};
    oklab_to_linear(c, lin[0], lin[1], lin[2]);
    std::uint32_t out = 0;
    for (float v : lin) {
        v = linear_to_srgb(v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v));
        out = (out << 8) | static_cast<std::uint32_t>(v * 255.0f + 0.5f);
    }
    return out;
}

[[nodiscard]] inline float chroma(const Lab& c) noexcept { return std::sqrt(c.a * c.a + c.b * c.b); }
[[nodiscard]] inline float hue(const Lab& c) noexcept { return std::atan2(c.b, c.a); } // radians

//! L, C, h to sRGB. Keeps lightness and hue and gives up chroma until the colour exists in sRGB.
[[nodiscard]] inline std::uint32_t from_lch(float L, float C, float h) noexcept {
    const float ca = std::cos(h);
    const float sa = std::sin(h);
    Lab c{L, C * ca, C * sa};
    if (!in_gamut(c)) {
        float lo = 0.0f;
        float hi = C;
        for (int i = 0; i < 18; ++i) {
            const float mid = (lo + hi) * 0.5f;
            (in_gamut(Lab{L, mid * ca, mid * sa}) ? lo : hi) = mid;
        }
        c = Lab{L, lo * ca, lo * sa};
    }
    return to_rgb(c);
}

//! Below this chroma a colour reads as grey.
inline constexpr float grey_chroma = 0.035f;

//! The cover's colour made legible as an accent on a dark or light panel: the same hue, at a
//! lightness that stands out from the panel, colourful enough to read as a colour. A grey cover
//! gets an off-white (dark panel) or charcoal (light panel). Identical to foo_mediabar/foo_onscreendisplay.
[[nodiscard]] inline std::uint32_t accent_for_card(std::uint32_t rgb, bool light_card) noexcept {
    const Lab c = from_rgb(rgb);
    const float C = chroma(c);
    if (C < grey_chroma) {
        const float L = light_card ? (c.L < 0.36f ? c.L : 0.36f) : (c.L > 0.87f ? c.L : 0.87f);
        return to_rgb(Lab{L, 0.0f, 0.0f});
    }
    const float lo = light_card ? 0.44f : 0.68f;
    const float hi = light_card ? 0.58f : 0.83f;
    const float L = c.L < lo ? lo : (c.L > hi ? hi : c.L);
    return from_lch(L, C < 0.12f ? 0.12f : C, hue(c));
}

//! Win32 COLORREF (0x00BBGGRR) <-> 0x00RRGGBB.
[[nodiscard]] constexpr std::uint32_t rgb_from_colorref(std::uint32_t bgr) noexcept {
    return ((bgr & 0xffu) << 16) | (bgr & 0xff00u) | ((bgr >> 16) & 0xffu);
}
[[nodiscard]] constexpr std::uint32_t colorref_from_rgb(std::uint32_t rgb) noexcept {
    return rgb_from_colorref(rgb); // the same swap
}

// ---------------------------------------------------------------------------------------------
// Legibility against the strip background (foo_mediabar render/theme.h, on 0x00RRGGBB).

[[nodiscard]] inline float lightness(std::uint32_t rgb) noexcept { return from_rgb(rgb).L; }

//! sRGB relative luminance, the WCAG definition.
[[nodiscard]] inline float relative_luminance(std::uint32_t rgb) noexcept {
    return 0.2126f * srgb_to_linear(static_cast<float>((rgb >> 16) & 0xffu) / 255.0f) +
           0.7152f * srgb_to_linear(static_cast<float>((rgb >> 8) & 0xffu) / 255.0f) +
           0.0722f * srgb_to_linear(static_cast<float>(rgb & 0xffu) / 255.0f);
}

[[nodiscard]] inline float contrast_ratio(std::uint32_t a, std::uint32_t b) noexcept {
    const float la = relative_luminance(a);
    const float lb = relative_luminance(b);
    return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f);
}

//! A background at or above this OKLab lightness is a light theme.
inline constexpr float light_background_lightness = 0.6f;

//! What an accent has to clear against the strip: 3:1, the WCAG bar for non-text UI.
inline constexpr float accent_min_contrast = 3.0f;

//! Moves `rgb` away from `background` in OKLab lightness, 0.02 a step, until it clears
//! `min_ratio`. Hue is kept exactly, chroma as far as sRGB allows.
[[nodiscard]] inline std::uint32_t with_min_contrast(std::uint32_t rgb, std::uint32_t background,
                                                     float min_ratio) noexcept {
    rgb &= 0xffffffu;
    if (contrast_ratio(rgb, background) >= min_ratio) return rgb;
    const Lab lab = from_rgb(rgb);
    const float C = chroma(lab);
    const float h = hue(lab);
    const float step = lightness(background) < light_background_lightness ? 0.02f : -0.02f;
    float L = lab.L;
    for (int i = 0; i < 50 && contrast_ratio(rgb, background) < min_ratio; ++i) {
        L = std::clamp(L + step, 0.0f, 1.0f);
        rgb = from_lch(L, C, h);
        if (L <= 0.0f || L >= 1.0f) break;
    }
    return rgb;
}

//! A colour taken from a cover, made usable as an accent on `background`.
[[nodiscard]] inline std::uint32_t accent_for_background(std::uint32_t raw, std::uint32_t background) noexcept {
    const bool light = lightness(background) >= light_background_lightness;
    return with_min_contrast(accent_for_card(raw & 0xffffffu, light), background, accent_min_contrast);
}

} // namespace bettertabs::colour
