#pragma once

// Instrumentation behind the "Better Tabs: log performance" advanced setting. Off means one cached
// bool read per event and nothing else. Main-thread only.

#include <cstddef>
#include <cstdint>

namespace bettertabs::perf {

//! The advanced setting. Cheap (a cached config bool).
[[nodiscard]] bool enabled() noexcept;
//! Experimental: wrap a tab switch in WM_SETREDRAW (advanced setting, off by default).
[[nodiscard]] bool use_setredraw() noexcept;

//! Heap allocations made inside this DLL since it loaded (alloc_counter.cpp).
[[nodiscard]] std::uint64_t allocation_count() noexcept;

[[nodiscard]] std::uint64_t now() noexcept;
[[nodiscard]] double elapsed_ms(std::uint64_t start, std::uint64_t end) noexcept;

//! Strip paint statistics for one strip, reset whenever they are reported.
struct PaintStats {
    std::uint32_t paints{0};
    std::uint32_t allocating_paints{0};
    double total_ms{0.0};
    double worst_ms{0.0};
    std::uint64_t pixels{0};

    void add(double ms, std::uint64_t allocations, std::uint64_t area) noexcept {
        ++paints;
        if (allocations != 0) ++allocating_paints;
        total_ms += ms;
        if (ms > worst_ms) worst_ms = ms;
        pixels += area;
    }
};

} // namespace bettertabs::perf
