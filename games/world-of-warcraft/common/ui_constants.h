#ifndef UI_CONSTANTS_H
#define UI_CONSTANTS_H

#define UI_BASE_WIDTH  1.0f
#define UI_BASE_HEIGHT 1.0f
#define UI_MIN_ASPECT  (4.0f / 3.0f)
#define UI_FRAMEPOINT_SCALE 32767.0
#define UI_FONT_COORD_SCALE 1000.0f
#define UI_PIXEL_ASPECT (UI_MIN_ASPECT * UI_BASE_HEIGHT / UI_BASE_WIDTH) // y/x; 4:3 WoW pixels inside its normalized 1x1 scene

/* Classic has no Light*.dbc here; keep its explicit outdoor fallback shared by camera and renderer. */
#define WOW_WORLD_FAR_CLIP 700.0f
#define WOW_WORLD_FOG_START 500.0f
#define WOW_WORLD_FOG_END 650.0f
#define WOW_WORLD_FOG_START_STRING "500"
#define WOW_WORLD_FOG_END_STRING "650"
#define WOW_WORLD_FOG_RED 0.60f
#define WOW_WORLD_FOG_GREEN 0.70f
#define WOW_WORLD_FOG_BLUE 0.85f

/* Sun light colors. Classic has no Light*.dbc / .lit, so the authored sun
   path is unavailable; these use WoWee's documented no-DBC fallback tint
   (cool ambient, warm diffuse). Diffuse is halved relative to WoWee's
   (1.0,0.95,0.85) so ambient+diffuse stays <= 1.0 in the engine's non-HDR
   lighting (ambient 0.5 + diffuse 0.5 = full brightness at N.L = 1). */
#define WOW_LIGHT_AMBIENT_R 0.50f  // red; cool blue-tinted ambient floor
#define WOW_LIGHT_AMBIENT_G 0.50f  // green
#define WOW_LIGHT_AMBIENT_B 0.60f  // blue; slightly elevated for a cool ambient
#define WOW_LIGHT_DIFFUSE_R 0.50f  // red; warm directional, halved from WoWee's 1.0
#define WOW_LIGHT_DIFFUSE_G 0.475f // green
#define WOW_LIGHT_DIFFUSE_B 0.425f // blue
#define WOW_DAY_LENGTH_MS 86400000.0f // ms; one full synthesized sun cycle (24h) for the time-of-day sun

#endif
