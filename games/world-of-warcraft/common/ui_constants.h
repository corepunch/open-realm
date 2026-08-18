#ifndef UI_CONSTANTS_H
#define UI_CONSTANTS_H

#define UI_BASE_WIDTH  1.0f
#define UI_BASE_HEIGHT 1.0f
#define UI_MIN_ASPECT  (4.0f / 3.0f)
#define UI_FRAMEPOINT_SCALE 32767.0
#define UI_FONT_COORD_SCALE 1000.0f

/* Classic has no Light*.dbc here; keep its explicit outdoor fallback shared by camera and renderer. */
#define WOW_WORLD_FAR_CLIP 700.0f
#define WOW_WORLD_FOG_START 500.0f
#define WOW_WORLD_FOG_END 650.0f
#define WOW_WORLD_FOG_START_STRING "500"
#define WOW_WORLD_FOG_END_STRING "650"
#define WOW_WORLD_FOG_RED 0.60f
#define WOW_WORLD_FOG_GREEN 0.70f
#define WOW_WORLD_FOG_BLUE 0.85f

#endif
