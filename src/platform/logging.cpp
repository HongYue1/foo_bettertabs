#include <helpers/foobar2000+atl.h>

#include "logging.h"

#include <string>

#include "../version.h"

namespace bettertabs::log {

namespace {

void print(std::string_view message, bool warning) noexcept {
    try {
        std::string line;
        line.reserve(message.size() + 16);
        line += BETTERTABS_NAME ": ";
        line += message;
        if (warning) {
            console::warning(line.c_str());
        } else {
            console::print(line.c_str());
        }
    } catch (...) {
    }
}

} // namespace

void info(std::string_view message) noexcept { print(message, false); }
void warn(std::string_view message) noexcept { print(message, true); }

} // namespace bettertabs::log
