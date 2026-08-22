#ifndef UI_CONSTANTS_H
#define UI_CONSTANTS_H

#define UI_BASE_WIDTH  1600.0f
#define UI_BASE_HEIGHT 1200.0f
#define UI_MIN_ASPECT  (4.0f / 3.0f)
#define UI_FRAMEPOINT_SCALE (32767.0 / 1600.0)
#define UI_FONT_COORD_SCALE 1.0f
#define UI_PIXEL_ASPECT (UI_MIN_ASPECT * UI_BASE_HEIGHT / UI_BASE_WIDTH) // y/x; square authored pixels in UI coordinates

#endif
