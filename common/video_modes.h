#ifndef video_modes_h
#define video_modes_h

#include "shared.h"

#define BZ_VIDEO_MODE_DEFAULT 0 // resolution index; 640x480 fits the lowest supported display class

typedef struct videomode_s {
    uint32_t width, height;
} videoMode_t;



static videoMode_t const video_modes[] = {
    { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1152, 864 }, { 1280, 720 },
    { 1280, 960 }, { 1280, 1024 }, { 1366, 768 }, { 1600, 900 }, { 1600, 1200 },
    { 1920, 1080 }, { 1920, 1200 }, { 2560, 1440 }, { 1280, 800 },
};

static inline uint32_t video_mode_count(void) { return sizeof(video_modes) / sizeof(*video_modes); }
static inline videoMode_t const * video_mode_get(int mode) {
    return mode >= 0 && mode < (int)video_mode_count() ? video_modes + mode : video_modes + BZ_VIDEO_MODE_DEFAULT;
}

#endif
