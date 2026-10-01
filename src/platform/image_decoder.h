#pragma once

// WIC, wrapped (from foo_mediabar): encoded image bytes -> premultiplied BGRA at a bounded size.
// Meant for a worker thread; it initialises COM for the duration when the thread has none.

#include <cstdint>
#include <optional>
#include <span>

#include "../model/cover_accent.h"

namespace bettertabs {

//! Downscales so neither edge exceeds `max_edge`; never upscales. Nothing on any failure.
[[nodiscard]] std::optional<DecodedImage> decode_image(std::span<const std::uint8_t> bytes,
                                                       std::uint32_t max_edge) noexcept;

} // namespace bettertabs
