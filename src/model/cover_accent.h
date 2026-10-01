#pragma once

// One colour out of a cover (ported from foo_mediabar).
//
// Runs on the worker that just decoded the cover, once per cover. Returns the cover's raw colour
// as 0xAARRGGBB with alpha forced opaque (OKLab hue voting, see cover_accent.cpp), or nothing
// when the cover has no opaque pixels. The raw colour is not legible as is; the host passes it
// through colour::accent_for_background against the strip background.

#include <cstdint>
#include <optional>
#include <vector>

namespace bettertabs {

//! Premultiplied BGRA, top-down, tightly packed.
struct DecodedImage {
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::vector<std::uint8_t> pixels;

    [[nodiscard]] std::uint32_t stride() const noexcept { return width * 4u; }
    [[nodiscard]] bool valid() const noexcept {
        return width > 0 && height > 0 && pixels.size() == static_cast<std::size_t>(width) * height * 4u;
    }
};

//! Deterministic: the same pixels always give the same colour.
[[nodiscard]] std::optional<std::uint32_t> extract_cover_accent(const DecodedImage& image) noexcept;

} // namespace bettertabs
