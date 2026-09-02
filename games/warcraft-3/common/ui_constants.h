#ifndef UI_CONSTANTS_H
#define UI_CONSTANTS_H

#define UI_BASE_WIDTH  0.8f // FDF units; retail WC3 virtual-canvas width; used by all horizontal UI geometry
#define UI_BASE_HEIGHT 0.6f // FDF units; retail WC3 virtual-canvas height; used by all vertical UI geometry
#define UI_MIN_ASPECT  (4.0f / 3.0f) // ratio; retail WC3 canvas aspect; bounds pillarboxed UI scenes
#define UI_FRAMEPOINT_SCALE 32767.0 // signed wire units per FDF unit; packs layout anchor offsets
#define UI_FONT_COORD_SCALE 1000.0f // font coordinates per FDF unit; converts authored font heights
#define UI_PIXEL_ASPECT (UI_MIN_ASPECT * UI_BASE_HEIGHT / UI_BASE_WIDTH) // y/x; square authored pixels in UI coordinates
#define BZ_WC3_WINDOW_QUEST MAKEFOURCC('Q','U','S','T') // opaque class/instance ID; identifies the singleton Quest window
#define BZ_WC3_WINDOW_LOG   MAKEFOURCC('L','O','G',' ') // opaque class/instance ID; identifies the singleton Message Log window

#endif
