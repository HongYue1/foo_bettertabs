#pragma once

// The now-playing cover's accent colour, shared by every container whose accent source is
// "cover". Main thread only.
//
// Costs nothing until the first subscriber: only then does it register with fb2k's now-playing
// art loader (which reads and caches the file once for all components) and a play callback.
// A cover is decoded (WIC, at most 256 px) and reduced to one colour on a worker, once per
// cover; recent covers are remembered by content hash, so skipping back costs nothing.
// After the last subscriber leaves, everything is unregistered again.

#include <cstdint>
#include <optional>

#include "fbc/cover_accent.h"

namespace bettertabs::cover {

class Listener {
public:
    //! The accent changed (a new cover, or none). Read it with current().
    virtual void on_cover_accent_changed() noexcept = 0;

protected:
    ~Listener() = default;
};

void subscribe(Listener* listener) noexcept;
void unsubscribe(Listener* listener) noexcept;

//! The raw cover colour, 0x00RRGGBB, or nothing (no cover, stopped, not decoded yet).
[[nodiscard]] std::optional<std::uint32_t> current() noexcept;
//! Everything known about the cover's colours (primary, secondary, colourfulness), or nothing.
[[nodiscard]] std::optional<fbc::CoverColours> current_colours() noexcept;

//! On quit: drop every registration.
void shutdown() noexcept;

} // namespace bettertabs::cover
