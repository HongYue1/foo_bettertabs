#pragma once

// Process-wide Direct2D / DirectWrite factories and DPI, all Windows 7 safe. d2d1.dll and
// dwrite.dll exist on Windows 7 SP1; everything newer is looked up at run time.

#include <windows.h>

#include <d2d1.h>
#include <dwrite.h>

namespace bettertabs::gfx {

//! Created on first use, released by shutdown(). Null if creation failed.
[[nodiscard]] ID2D1Factory* d2d() noexcept;
[[nodiscard]] IDWriteFactory* dwrite() noexcept;
void shutdown() noexcept;

//! GetDpiForWindow where it exists (Windows 10 1607+), else the screen's LOGPIXELSX.
[[nodiscard]] unsigned window_dpi(HWND wnd) noexcept;
//! The DPI that GDI font metrics (LOGFONT from Columns UI) are expressed in.
[[nodiscard]] unsigned system_dpi() noexcept;

//! Windows 8 or later: child windows may be WS_EX_LAYERED.
[[nodiscard]] bool layered_children_supported() noexcept;

//! Direct2D 1.2+ (Windows 8.1): D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT is understood.
[[nodiscard]] bool colour_fonts_supported() noexcept;

//! ClearType is on in Windows.
[[nodiscard]] bool system_uses_cleartype() noexcept;

//! Loads DirectWrite's process-wide caches (system font collection, system font fallback, a
//! shaped layout, monitor rendering parameters) so the first container does not pay for them on
//! the main thread (~150 ms cold, measured in 0.3.3). Safe on any thread: it uses its own
//! reference to the shared factory and touches no other state here. Returns milliseconds spent.
double warm_text() noexcept;
//! For the perf log: "none" (never started), "running", or the milliseconds warm_text() took.
void warm_text_status(char* out, unsigned size) noexcept;

} // namespace bettertabs::gfx
