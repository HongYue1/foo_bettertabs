// Offline check of the cover accent (ported from foo_mediabar/test/accent_test.cpp): synthetic
// covers through extract_cover_accent and accent_for_background, on a dark and a light strip.
// Built and run by test\build_tests.bat.
//
// Also cross-checks model/colour.h against foo_onscreendisplay/src/colour.h, which it was ported from.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <vector>

#include "../../foo_onscreendisplay/src/colour.h"
#include "../src/model/colour.h"
#include "../src/model/cover_accent.h"

using namespace bettertabs;

namespace {

constexpr std::uint32_t side = 256;

double now_ms() {
    LARGE_INTEGER f{};
    LARGE_INTEGER t{};
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart) * 1000.0 / static_cast<double>(f.QuadPart);
}

struct Cover {
    const char* name;
    std::uint32_t (*px)(int x, int y);
};

const Cover covers[] = {
    {"gradient checker", [](int x, int y) -> std::uint32_t {
         return (static_cast<std::uint32_t>(200 - y * 120 / 256) << 16) |
                (static_cast<std::uint32_t>(60 + x * 100 / 256) << 8) |
                (120u + static_cast<std::uint32_t>((x / 32 + y / 32) % 2) * 60u);
     }},
    {"khaki + 12% blue", [](int x, int y) -> std::uint32_t {
         const int dx = x - 170, dy = y - 90;
         return dx * dx + dy * dy < 50 * 50 ? 0x2A7BF0u : (y > 200 ? 0x5C4A32u : 0xA89A6Eu);
     }},
    {"greyscale", [](int x, int y) -> std::uint32_t {
         const auto v = static_cast<std::uint32_t>(40 + (x + y) * 170 / 512);
         return (v << 16) | (v << 8) | v;
     }},
    {"navy + 4% orange", [](int x, int y) -> std::uint32_t {
         return (x > 100 && x < 150 && y > 100 && y < 152) ? 0xF28A1Eu : 0x14203Au;
     }},
    {"black + red stripe", [](int, int y) -> std::uint32_t { return (y > 120 && y < 146) ? 0xC81E24u : 0x0A0A0Cu; }},
    {"pastel pink + white", [](int x, int) -> std::uint32_t { return x < 150 ? 0xF4C6D2u : 0xFAFAFAu; }},
    {"yellow + black type", [](int x, int y) -> std::uint32_t {
         return (y > 180 && y < 210 && (x / 12) % 2) ? 0x111111u : 0xF2D21Bu;
     }},
    {"teal / orange", [](int x, int y) -> std::uint32_t { return x + y < 256 ? 0x1F7A80u : 0xE0772Fu; }},
    {"forest + skin", [](int x, int y) -> std::uint32_t {
         const int dx = x - 128, dy = y - 128;
         return dx * dx + dy * dy < 60 * 60 ? 0xD9A07Eu : (((x ^ y) & 8) ? 0x2E4A22u : 0x3F6130u);
     }},
};

//! Premultiplied BGRA, fully opaque, exactly what decode_image hands the extractor.
DecodedImage make_cover(const Cover& cover) {
    DecodedImage image;
    image.width = side;
    image.height = side;
    image.pixels.resize(static_cast<std::size_t>(side) * side * 4u);
    for (std::uint32_t y = 0; y < side; ++y) {
        for (std::uint32_t x = 0; x < side; ++x) {
            const std::uint32_t rgb = cover.px(static_cast<int>(x), static_cast<int>(y));
            std::uint8_t* px = image.pixels.data() + (static_cast<std::size_t>(y) * side + x) * 4u;
            px[0] = static_cast<std::uint8_t>(rgb & 0xffu);
            px[1] = static_cast<std::uint8_t>((rgb >> 8) & 0xffu);
            px[2] = static_cast<std::uint8_t>((rgb >> 16) & 0xffu);
            px[3] = 0xff;
        }
    }
    return image;
}

} // namespace

int main() {
    // Typical host backgrounds: DUI dark and light defaults.
    constexpr std::uint32_t dark_bg = 0x1f1f1fu;
    constexpr std::uint32_t light_bg = 0xffffffu;
    int failures = 0;

    for (const Cover& cover : covers) {
        const DecodedImage image = make_cover(cover);
        std::optional<std::uint32_t> raw;
        constexpr int runs = 20;
        const double t0 = now_ms();
        for (int i = 0; i < runs; ++i) raw = extract_cover_accent(image);
        const double ms = (now_ms() - t0) / runs;
        if (!raw) {
            std::printf("accent %-20s none (%.2f ms)\n", cover.name, ms);
            ++failures;
            continue;
        }
        const std::uint32_t on_dark = colour::accent_for_background(*raw, dark_bg);
        const std::uint32_t on_light = colour::accent_for_background(*raw, light_bg);
        const double cr_dark = colour::contrast_ratio(on_dark, dark_bg);
        const double cr_light = colour::contrast_ratio(on_light, light_bg);
        std::printf("accent %-20s raw #%06X -> dark #%06X (%.1f:1), light #%06X (%.1f:1)  %.3f ms\n", cover.name,
                    *raw & 0xffffffu, on_dark, cr_dark, on_light, cr_light, ms);
        if (cr_dark < colour::accent_min_contrast - 0.01 || cr_light < colour::accent_min_contrast - 0.01) ++failures;
        // Once per cover on a worker; generous, so a busy machine does not fail the run.
        if (ms > 5.0) ++failures;

        // The port must agree with the original for the legibility step.
        const std::uint32_t rgb = *raw & 0xffffffu;
        if (colour::accent_for_card(rgb, false) != osd::colour::accentForCard(rgb, false) ||
            colour::accent_for_card(rgb, true) != osd::colour::accentForCard(rgb, true)) {
            std::printf("  MISMATCH with foo_onscreendisplay accentForCard\n");
            ++failures;
        }
    }

    // Translucent covers: nothing opaque means no accent, not a guess.
    {
        DecodedImage clear = make_cover(covers[0]);
        for (std::size_t i = 3; i < clear.pixels.size(); i += 4) clear.pixels[i] = 0;
        const bool none = !extract_cover_accent(clear).has_value();
        std::printf("transparent cover -> %s\n", none ? "no accent (ok)" : "accent (WRONG)");
        if (!none) ++failures;
    }

    {
        // A yellow cover on a light strip: the line accent goes dark (olive), the solid-fill
        // accent keeps it yellow and carries black text.
        const std::uint32_t yellow = 0xE6C81Eu;
        const std::uint32_t line = colour::accent_for_background(yellow, 0xFFFFFFu);
        const std::uint32_t fill = colour::accent_for_card(yellow, false);
        const float L = colour::from_rgb(fill).L;
        const float black = colour::contrast_ratio(0x000000u, fill);
        const float dh = std::fabs(colour::hue(colour::from_rgb(fill)) - colour::hue(colour::from_rgb(yellow)));
        std::printf("yellow on light: line %06X, fill %06X (L %.2f, black text %.1f:1, hue shift %.3f)\n", line,
                    fill, L, black, dh);
        if (L < 0.68f || black < 4.5f || dh > 0.05f) ++failures;
    }
    std::printf("failures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
