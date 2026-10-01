#pragma once

// Small helpers shared by the containers (Columns UI and Default UI).

#include <helpers/foobar2000+atl.h>

#include <cstdint>
#include <string>

#include "../model/codec.h"

namespace bettertabs {

[[nodiscard]] inline std::wstring widen(const char* utf8) {
    const pfc::stringcvt::string_wide_from_utf8 wide(utf8 != nullptr ? utf8 : "");
    return std::wstring(wide.get_ptr());
}

[[nodiscard]] inline pfc::string8 narrow(const std::wstring& wide) {
    return pfc::string8(pfc::stringcvt::string_utf8_from_wide(wide.c_str()).get_ptr());
}

[[nodiscard]] inline Bytes to_bytes(const void* data, std::size_t size) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    return size != 0 && p != nullptr ? Bytes(p, p + size) : Bytes{};
}

//! Menu text: a lone '&' would underline the next letter.
[[nodiscard]] inline std::wstring menu_text(const std::wstring& label) {
    std::wstring out;
    out.reserve(label.size() + 4);
    for (const wchar_t c : label) {
        if (c == L'&') out += L'&';
        out += c;
    }
    return out;
}

[[nodiscard]] inline bool same_rect(const RECT& a, const RECT& b) noexcept {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

//! A code point as UTF-16; empty for none or anything that is not a scalar value.
[[nodiscard]] inline std::wstring icon_text(std::uint32_t cp) {
    if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return {};
    if (cp < 0x10000) return std::wstring(1, static_cast<wchar_t>(cp));
    cp -= 0x10000;
    return std::wstring{static_cast<wchar_t>(0xD800 + (cp >> 10)), static_cast<wchar_t>(0xDC00 + (cp & 0x3FF))};
}

} // namespace bettertabs
