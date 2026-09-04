#ifndef video_modes_h
#define video_modes_h

#include "shared.h"

#define BZ_VIDEO_MODE_DEFAULT 0 // resolution index; 640x480 fits the lowest supported display class

typedef struct VIDEOMODE {
    DWORD width, height;
} VIDEOMODE;
typedef struct VIDEOMODE *LPVIDEOMODE;
typedef const struct VIDEOMODE *LPCVIDEOMODE;

static VIDEOMODE const video_modes[] = {
    { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1152, 864 }, { 1280, 720 },
    { 1280, 960 }, { 1280, 1024 }, { 1366, 768 }, { 1600, 900 }, { 1600, 1200 },
    { 1920, 1080 }, { 1920, 1200 }, { 2560, 1440 }, { 1280, 800 },
};

static inline DWORD video_mode_count(void) { return sizeof(video_modes) / sizeof(*video_modes); }
static inline LPCVIDEOMODE video_mode_get(int mode) {
    return mode >= 0 && mode < (int)video_mode_count() ? video_modes + mode : video_modes + BZ_VIDEO_MODE_DEFAULT;
}

#endif
