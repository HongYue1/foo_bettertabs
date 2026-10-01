#pragma once

// Every GUID this component owns. Fresh, never copied from a sample. Changing one breaks
// every saved layout that contains it, so these are frozen once shipped.

#include <guiddef.h>

namespace bettertabs::guids {

//! The container's uie::window identity. Stored in every layout that uses it.
inline constexpr GUID container = {
    0xb66b7d07, 0x9ad9, 0x4a82, {0xbf, 0xd5, 0x5b, 0x38, 0x54, 0x3f, 0x4a, 0x1f}};

//! The Default UI element's identity (0.5.0). Stored in every Default UI layout that uses it.
inline constexpr GUID dui_element = {
    0x51a3d9b6, 0xf203, 0x4349, {0x94, 0x17, 0x55, 0x18, 0xcc, 0x1b, 0x1d, 0x63}};

//! Our uie::window_host type. Constant across instances, like Tab stack's.
inline constexpr GUID host = {
    0xeb9ced50, 0xee26, 0x4d77, {0x85, 0x22, 0xa4, 0x2b, 0xe0, 0x02, 0x89, 0x28}};

//! Format ID of our splitter_item_full_v3 extra data (codec.h, TabExtra).
inline constexpr GUID tab_extra_format = {
    0x10293b75, 0x9ddb, 0x43c0, {0x98, 0xc8, 0x1e, 0x35, 0x9f, 0x02, 0x7a, 0x18}};

//! Columns UI colour and font clients (M(b)).
inline constexpr GUID colour_client = {
    0xed4aae10, 0x5e24, 0x48b9, {0xa8, 0x2f, 0x64, 0xaf, 0x36, 0x05, 0xc3, 0xc6}};
inline constexpr GUID font_client = {
    0x6d075fa0, 0xa302, 0x4a06, {0x9e, 0x15, 0x68, 0x3c, 0x32, 0x4f, 0x1e, 0x63}};

//! Advanced preferences: Display > Better Tabs: log performance.
inline constexpr GUID advconfig_perf = {
    0x8d873fcd, 0x9e52, 0x4fe6, {0x8d, 0xc8, 0x6f, 0xba, 0xe7, 0x12, 0x3b, 0x58}};
//! Advanced preferences: wrap tab switches in WM_SETREDRAW (an experiment, off by default).
inline constexpr GUID advconfig_setredraw = {
    0xc5621a99, 0xb628, 0x459c, {0x85, 0x68, 0x88, 0x5b, 0x50, 0x0c, 0x15, 0x64}};

} // namespace bettertabs::guids
