#pragma once

// Console breadcrumbs. Recent console output is embedded in fb2k crash reports, so these are worth
// having - but never from a paint path.

#include <string_view>

namespace bettertabs::log {

void info(std::string_view message) noexcept;
void warn(std::string_view message) noexcept;

} // namespace bettertabs::log
