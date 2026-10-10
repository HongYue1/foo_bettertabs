#pragma once

// The point size behind a GDI font height. No SDK, no Win32, so the offline tests can use it.

#include <cmath>
#include <cstdint>
#include <initializer_list>

namespace bettertabs {

//! Tenths of a point for a font `pixels` high (the em, |lfHeight|) at `dpi`. A LOGFONT keeps
//! whole pixels, so the size the user picked is lost: 8 pt at 96 DPI is 11 px, which reads back
//! as 8.25 pt. The answer is the nearest whole point, else the nearest half point, that gives the
//! same whole number of pixels; otherwise the exact value.
[[nodiscard]] inline std::uint32_t tenths_from_pixels(float pixels, unsigned dpi) noexcept {
    if (pixels <= 0.0f) return 0;
    const float d = static_cast<float>(dpi != 0 ? dpi : 96);
    const float exact = pixels * 72.0f / d;
    const long target = std::lround(pixels);
    for (const float step : {1.0f, 0.5f}) {
        const float points = std::round(exact / step) * step;
        if (points > 0.0f && std::lround(points * d / 72.0f) == target) {
            return static_cast<std::uint32_t>(std::lround(points * 10.0f));
        }
    }
    return static_cast<std::uint32_t>(std::lround(exact * 10.0f));
}

} // namespace bettertabs
