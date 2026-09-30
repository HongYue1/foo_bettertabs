#include <helpers/foobar2000+atl.h>

#include "perf.h"

#include "../guids.h"

namespace bettertabs::perf {

namespace {

advconfig_checkbox_factory_cached g_perf("Better Tabs: log performance to the console",
                                         "foo_bettertabs.log_performance", guids::advconfig_perf,
                                         advconfig_branch::guid_branch_display, 0, false);

advconfig_checkbox_factory_cached g_setredraw(
    "Better Tabs: wrap tab switches in WM_SETREDRAW (experiment)", "foo_bettertabs.setredraw",
    guids::advconfig_setredraw, advconfig_branch::guid_branch_display, 0, false);

[[nodiscard]] double ticks_per_ms() noexcept {
    static const double value = [] {
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        return static_cast<double>(frequency.QuadPart) / 1000.0;
    }();
    return value;
}

} // namespace

bool enabled() noexcept {
    try {
        return g_perf.get();
    } catch (...) {
        return false;
    }
}

bool use_setredraw() noexcept {
    try {
        return g_setredraw.get();
    } catch (...) {
        return false;
    }
}

std::uint64_t now() noexcept {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<std::uint64_t>(counter.QuadPart);
}

double elapsed_ms(std::uint64_t start, std::uint64_t end) noexcept {
    return static_cast<double>(end - start) / ticks_per_ms();
}

} // namespace bettertabs::perf
