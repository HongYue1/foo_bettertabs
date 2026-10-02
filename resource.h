#pragma once

// Resource IDs, shared by foo_bettertabs.rc and the C++ side. This file is included by the
// resource compiler, so it must stay free of C++: #define only, no namespaces, no constexpr.
//
// IDC_STATIC (-1) comes from winres.h and is used for labels nobody needs to address.

#define IDD_CONFIGURE 101
// The Configure dialog's pages, consecutive and in tab order.
#define IDD_PAGE_STRIP 102
#define IDD_PAGE_LOOK 103
#define IDD_PAGE_TABS 104
#define IDD_PAGE_BEHAVIOUR 105
#define IDD_PAGE_AUTOHIDE 106
#define IDD_RENAME 110

#define IDC_TABS 1000
// Where the pages go; never shown.
#define IDC_PAGE_HOST 1001
#define IDC_DEFAULTS 1002

// Strip
#define IDC_POSITION 1010
#define IDC_VISIBILITY 1011
#define IDC_ROTATE 1012
#define IDC_SIZING 1013
#define IDC_ALIGN 1014
#define IDC_PAD_X 1015
#define IDC_PAD_Y 1016
#define IDC_SPACING 1017
#define IDC_THICKNESS 1018
#define IDC_MAX_WIDTH 1019

// Look
#define IDC_INDICATOR 1030
#define IDC_CHIP 1031
#define IDC_RADIUS 1032
#define IDC_STRENGTH_AUTO 1033
#define IDC_STRENGTH 1034
#define IDC_STRENGTH_VALUE 1035
#define IDC_ACCENT_SOURCE 1036
#define IDC_ACCENT_HEX 1037
#define IDC_ACCENT_SWATCH 1038
#define IDC_BACKGROUND 1039
#define IDC_BACKGROUND_HEX 1040
#define IDC_BACKGROUND_SWATCH 1041
#define IDC_TINT 1042
#define IDC_TINT_VALUE 1043

// Tabs
#define IDC_TAB_LIST 1050
#define IDC_MOVE_UP 1051
#define IDC_MOVE_DOWN 1052
#define IDC_TAB_TITLE 1053
#define IDC_TAB_TITLE_HELP 1054
#define IDC_TAB_FORMAT 1055
#define IDC_TAB_ICON 1056
#define IDC_TAB_ICON_PREVIEW 1057
#define IDC_TAB_HIDDEN 1058
#define IDC_TAB_ON_PLAY 1059
#define IDC_TAB_ON_STOP 1060
#define IDC_REMOVE 1061
#define IDC_CHARMAP 1062

// Behaviour
#define IDC_WHEEL 1070
#define IDC_DRAG 1071
#define IDC_CTRL_TAB 1072
#define IDC_MIDDLE 1073
#define IDC_LAZY 1074
#define IDC_REMEMBER 1075
#define IDC_ICONS_ONLY 1076
#define IDC_SWITCH_ANIM 1077
#define IDC_SWITCH_MS 1078

// Auto-hide
#define IDC_AH_MODE 1080
#define IDC_AH_ANIM 1081
#define IDC_AH_ANIM_MS 1082
#define IDC_AH_HOT_ZONE 1083
#define IDC_AH_REVEAL 1084
#define IDC_AH_HIDE 1085
#define IDC_AH_LINGER 1086

// Rename
#define IDC_RENAME_TITLE 1090
#define IDC_RENAME_HELP 1091
#define IDC_RENAME_FORMAT 1092
#define IDC_CHEVRON 1093
#define IDC_SHRINK 1094
