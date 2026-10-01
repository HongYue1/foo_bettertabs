// Ported unchanged from foo_mediabar/src/model/cover_accent.cpp (same author, same algorithm),
// so a cover gives the same accent in Better Tabs, Media Bar and foo_osd.

#include "cover_accent.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include "colour.h"

namespace bettertabs {

namespace {

constexpr int hue_bins = 36;          // 10 degrees each
constexpr int grey_bins = 16;
constexpr float corner_weight = 0.30f; // weight of a corner pixel relative to the centre
constexpr double min_colour = 0.02;    // colourful share of the weight below which it is grey
constexpr float pi = 3.14159265f;

//! The decode is capped at 256x256, so a full pass is at most 65k pixels. Subsampling to ~16k
//! keeps it well under a millisecond and changes the answer by nothing visible.
constexpr std::size_t max_samples = 16384;

[[nodiscard]] float smoothstep(float e0, float e1, float x) noexcept {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

//! sRGB byte -> linear light. 256 entries, built once; the pow() is the expensive part of the
//! conversion and there are only 256 distinct inputs.
const std::array<float, 256>& linear_lut() noexcept {
    static const std::array<float, 256> table = [] {
        std::array<float, 256> t{};
        for (std::size_t i = 0; i < t.size(); ++i) {
            t[i] = colour::srgb_to_linear(static_cast<float>(i) / 255.0f);
        }
        return t;
    }();
    return table;
}

struct Bin {
    double w{0.0};
    double L{0.0};
    double a{0.0};
    double b{0.0};
    double c{0.0};
};

} // namespace

// The cover's colour, in OKLab (see colour.h), in four steps:
//  1. Every sampled pixel gets a weight: the centre of the picture counts more than its edges
//     (borders, logos, barcodes), and near-black / near-white pixels count little.
//  2. Colourful pixels (chroma above grey) are binned by hue, 36 bins of 10 degrees, weighted
//     by how colourful they are and how usable their lightness is.
//  3. Each hue (with its neighbours, so a hue on a bin edge is not split in two) is scored by
//     population^0.7 x vividness. A large area wins, but a vivid area beats a larger dull one,
//     which is what reads as a cover's "colour": the red title on a brown sleeve, not the brown.
//  4. The winner is the weighted OKLab mean of its pixels: a real colour from the cover, not a
//     bucket centre, and averaged in a space where averaging does not turn to mud.
// A cover with almost no colourful pixels answers with its dominant grey. The result is the raw
// cover colour; colour::accent_for_card (via accent_for_background) makes it legible.
std::optional<std::uint32_t> extract_cover_accent(const DecodedImage& image) noexcept {
    if (!image.valid()) return std::nullopt;

    const auto& to_linear = linear_lut();
    std::array<Bin, hue_bins> hues{};
    std::array<Bin, grey_bins> greys{};
    double total = 0.0;
    double colourful = 0.0;

    const std::size_t width = image.width;
    const std::size_t pixels = width * image.height;
    const std::size_t step = std::max<std::size_t>(1, pixels / max_samples);
    const float half_w = static_cast<float>(image.width) * 0.5f;
    const float half_h = static_cast<float>(image.height) * 0.5f;
    const std::uint8_t* const data = image.pixels.data();

    for (std::size_t i = 0; i < pixels; i += step) {
        // Premultiplied BGRA. A translucent pixel is a border or a shadow, not the cover's
        // colour; the few 250..254 survivors are divided back out so they are not darkened.
        const std::uint8_t* const px = data + i * 4u;
        const unsigned alpha = px[3];
        if (alpha < 250u) continue;
        const auto channel = [&](std::uint8_t v) noexcept {
            const unsigned straight = alpha == 255u ? v : std::min(255u, (v * 255u + alpha / 2u) / alpha);
            return to_linear[straight];
        };
        const colour::Lab c = colour::linear_to_oklab(channel(px[2]), channel(px[1]), channel(px[0]));

        const float dx = (static_cast<float>(i % width) + 0.5f - half_w) / half_w;
        const float dy = (static_cast<float>(i / width) + 0.5f - half_h) / half_h;
        const float radius = std::min(1.0f, std::sqrt(dx * dx + dy * dy) * 0.70710678f);
        float w = 1.0f - (1.0f - corner_weight) * radius * radius;
        // Near black and near white say little about a cover's colour.
        w *= 0.15f + 0.85f * smoothstep(0.10f, 0.22f, c.L) * (1.0f - smoothstep(0.93f, 0.99f, c.L));
        total += w;

        const float C = colour::chroma(c);
        const float vivid = smoothstep(colour::grey_chroma, 0.12f, C);
        if (vivid > 0.0f) {
            colourful += static_cast<double>(w * vivid);
            // Lightness where an accent can live; very dark or pale colours are a last resort.
            const float usable =
                0.35f + 0.65f * smoothstep(0.22f, 0.40f, c.L) * (1.0f - smoothstep(0.88f, 0.97f, c.L));
            const double cw = static_cast<double>(w * vivid * usable);
            float h = colour::hue(c);
            if (h < 0.0f) h += 2.0f * pi;
            Bin& bin = hues[static_cast<std::size_t>(
                std::min(hue_bins - 1, static_cast<int>(h / (2.0f * pi) * hue_bins)))];
            bin.w += cw;
            bin.L += cw * c.L;
            bin.a += cw * c.a;
            bin.b += cw * c.b;
            bin.c += cw * C;
        }
        if (C < 0.06f) {
            Bin& g = greys[static_cast<std::size_t>(
                std::clamp(static_cast<int>(c.L * grey_bins), 0, grey_bins - 1))];
            g.w += w;
            g.L += static_cast<double>(w) * c.L;
        }
    }
    if (total <= 0.0) return std::nullopt; // nothing opaque at all

    colour::Lab pick{};
    if (colourful >= min_colour * total) {
        int best = -1;
        double best_score = 0.0;
        for (int i = 0; i < hue_bins; ++i) {
            const Bin& l = hues[static_cast<std::size_t>((i + hue_bins - 1) % hue_bins)];
            const Bin& m = hues[static_cast<std::size_t>(i)];
            const Bin& r = hues[static_cast<std::size_t>((i + 1) % hue_bins)];
            const double pop = 0.5 * l.w + m.w + 0.5 * r.w;
            if (pop <= 0.0) continue;
            const double mean_c = (0.5 * l.c + m.c + 0.5 * r.c) / pop;
            const double score = std::pow(pop / total, 0.7) * (0.5 + 4.0 * mean_c);
            if (score > best_score) {
                best_score = score;
                best = i;
            }
        }
        if (best < 0) return std::nullopt;
        Bin sum{};
        for (int d = -1; d <= 1; ++d) {
            const Bin& b = hues[static_cast<std::size_t>((best + d + hue_bins) % hue_bins)];
            sum.w += b.w;
            sum.L += b.L;
            sum.a += b.a;
            sum.b += b.b;
        }
        if (sum.w <= 0.0) return std::nullopt;
        pick = colour::Lab{static_cast<float>(sum.L / sum.w), static_cast<float>(sum.a / sum.w),
                           static_cast<float>(sum.b / sum.w)};
    } else {
        // Monochrome: the most common grey level, averaged with its neighbours. An answer, not
        // a failure - the view turns it into a legible off-white or charcoal.
        int best = 0;
        for (int i = 1; i < grey_bins; ++i) {
            if (greys[static_cast<std::size_t>(i)].w > greys[static_cast<std::size_t>(best)].w) best = i;
        }
        double w = 0.0;
        double L = 0.0;
        for (int d = -1; d <= 1; ++d) {
            const int k = best + d;
            if (k < 0 || k >= grey_bins) continue;
            w += greys[static_cast<std::size_t>(k)].w;
            L += greys[static_cast<std::size_t>(k)].L;
        }
        if (w <= 0.0) return std::nullopt;
        pick = colour::Lab{static_cast<float>(L / w), 0.0f, 0.0f};
    }
    return 0xff000000u | colour::to_rgb(pick);
}

} // namespace bettertabs
