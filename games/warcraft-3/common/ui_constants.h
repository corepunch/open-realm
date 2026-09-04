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
#define BZ_WC3_WINDOW_MENU  MAKEFOURCC('M','E','N','U') // opaque class/instance ID; identifies the singleton pause menu window
#define BZ_WC3_WINDOW_ALLIES MAKEFOURCC('A','L','L','Y') // opaque class/instance ID; identifies the singleton Allies window
#define BZ_WC3_WINDOW_RESULT MAKEFOURCC('R','S','L','T') // opaque class/instance ID; identifies the singleton victory/defeat result window
#define WC3_MODAL_QUEST  (1u << 0) // modal owner bit; retained for the Quest/JASS ownership compatibility path
#define WC3_MODAL_CLIENT (1u << 1) // modal owner bit; tracks whether the client has any open modal window
#define WC3_CAMERA_DEFAULT_FOV 50.0f // degrees; vertical FOV; spawn, ResetToGameCamera, CL_GameDefaultCamera
#define WC3_CAMERA_DEFAULT_DISTANCE 1650.0f // world units; orbit distance; spawn, ResetToGameCamera, CL_GameDefaultCamera
#define WC3_CAMERA_DEFAULT_PITCH 326.0f // Euler degrees; JASS AoA 304 wraps via -90-AoA; spawn, ResetToGameCamera
#define WC3_CAMERA_DEFAULT_YAW 0.0f // Euler degrees; JASS rotation 90 stores as 90-rotation; spawn, ResetToGameCamera
#define WC3_CAMERA_DEFAULT_NEAR_Z 100.0f // world units; retail default near clip; spawn, ResetToGameCamera, CameraSetupCreate
#define WC3_CAMERA_DEFAULT_FAR_Z 5000.0f // world units; retail default far clip; spawn, ResetToGameCamera, CameraSetupCreate

#endif
