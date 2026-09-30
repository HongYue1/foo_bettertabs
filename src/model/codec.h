#pragma once

// The two stored formats: the instance blob (set_config / get_config) and the per-tab extra data
// (splitter_item_full_v3). Pure code, no SDK: the container adapts SDK streams to byte spans,
// and test/codec_test.cpp exercises everything here offline.
//
// Both formats are tag/length/value, little-endian, and built for the two directions that break
// naive blobs: an older build reading a newer blob (unknown tags are skipped, and kept verbatim
// so the next save writes them back) and a newer build reading an older blob (missing tags keep
// their defaults). Nothing here fails: a blob reads as far as it is valid, then defaults.
//
// Instance blob:
//
//     'B' 'T' 'A' 'B'  uint16 format_version  uint16 min_reader_version
//     then sections until the end:  uint16 tag  uint32 length  bytes[length]
//       1 settings  fields: uint16 id  uint16 length  bytes    (ids in codec.cpp, frozen)
//       2 children  uint32 count, then per child:
//                   GUID (16 bytes, Data1..3 little-endian)  uint32 n  config[n]  uint32 m  extra[m]
//       3 state     uint32 active tab index
//
// Tab extra (format GUID guids::tab_extra_format):
//
//     uint16 version  then fields: uint16 id  uint16 length  bytes
//       1 title (UTF-8)  2 title is title-format  3 hidden  4 icon code point (uint32)
//       5 icon font family (UTF-8)  6 use custom title

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <guiddef.h>

#include "settings.h"

namespace bettertabs {

using Bytes = std::vector<std::uint8_t>;

//! A field or section this build does not understand, kept so it survives a save.
struct RawField {
    std::uint16_t id{0};
    Bytes data;
    [[nodiscard]] bool operator==(const RawField&) const = default;
};

struct TabExtra {
    bool use_custom_title{false};
    std::string title;
    bool title_is_format{false};
    bool hidden{false};
    //! Unicode code point of an icon glyph, 0 = none.
    std::uint32_t icon{0};
    std::string icon_font;
    std::vector<RawField> unknown;

    [[nodiscard]] bool operator==(const TabExtra&) const = default;
};

struct ChildRecord {
    GUID guid{};
    Bytes config;
    Bytes extra;
};

struct InstanceData {
    Settings settings;
    std::vector<RawField> unknown_settings;
    std::vector<ChildRecord> children;
    std::uint32_t active{0};
    std::vector<RawField> unknown_sections;
};

inline constexpr std::uint16_t instance_format_version = 1;
inline constexpr std::uint16_t tab_extra_version = 1;

[[nodiscard]] Bytes encode_instance(const InstanceData& data);
//! Throws only std::bad_alloc.
[[nodiscard]] InstanceData decode_instance(std::span<const std::uint8_t> bytes);

[[nodiscard]] Bytes encode_tab_extra(const TabExtra& extra);
//! Throws only std::bad_alloc.
[[nodiscard]] TabExtra decode_tab_extra(std::span<const std::uint8_t> bytes);

} // namespace bettertabs
