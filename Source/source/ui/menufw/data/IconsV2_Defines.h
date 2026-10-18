#pragma once

// IconsV2 Font - Custom Icon Font
// These icons share the same Unicode range as FontAwesome but come from a different font
// IconsV2 is loaded FIRST in fonts.cpp so it has priority over FontAwesome

#define ICON_MIN_V2 0xF000
#define ICON_MAX_V2 0xF0FF

// Tab Icons - IconsV2 overrides FontAwesome for these codes
#define ICON_V2_AIMBOT      "\xEF\x80\x88"  // U+F008 - from IconsV2 font
#define ICON_V2_SETTINGS    "\xEF\x80\x8D"  // U+F00D - from IconsV2 font
#define ICON_V2_MISC        "\xEF\x80\x93"  // U+F013 - from IconsV2 font
#define ICON_V2_VISUALS     "\xEF\x81\x8B"  // U+F04B - from IconsV2 font
#define ICON_V2_CHEVRON_DOWN "\xEF\x82\x91"  // U+F091 - from IconsV2 font (Chevron Down/Closed)
#define ICON_V2_CHEVRON_RIGHT "\xEF\x82\x93"  // U+F093 - from IconsV2 font (Chevron Right/Opened)
