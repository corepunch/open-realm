#include "s_local.h"

#define MINIMP3_ONLY_MP3
#define MINIMP3_IMPLEMENTATION
#include "vendor/minimp3/minimp3.h"

#include <limits.h>
#include <stdint.h>

static BOOL s_mp3_reserve(sfxcache_t **cache, size_t *capacity, int needed) {
    size_t next = *capacity ? *capacity : 4096;
    sfxcache_t *grown;

    while (next < (size_t)needed) {
        if (next > (size_t)INT_MAX / 2) { next = (size_t)needed; break; }
        next *= 2;
    }
    if (next > (SIZE_MAX - sizeof(**cache)) / sizeof(short) + 1) return FALSE;
    grown = realloc(*cache, sizeof(**cache) + (next - 1) * sizeof(short));
    if (!grown) return FALSE;
    *cache = grown;
    *capacity = next;
    return TRUE;
}

sfxcache_t *s_mp3_decode(BYTE const *data, DWORD size) {
    mp3dec_t decoder;
    mp3dec_frame_info_t info;
    mp3d_sample_t frame[MINIMP3_MAX_SAMPLES_PER_FRAME];
    sfxcache_t *cache = NULL;
    size_t capacity = 0;
    double sample_pos = 0.0;
    int length = 0, offset = 0;

    if (!data || size < 4 || size > INT_MAX) return NULL;
    mp3dec_init(&decoder);
    while (offset < (int)size) {
        int remaining = (int)size - offset;
        int samples = mp3dec_decode_frame(&decoder, data + offset, remaining, frame, &info);

        if (info.frame_bytes <= 0 || info.frame_bytes > remaining) break;
        offset += info.frame_bytes;
        if (samples <= 0) continue;
        if ((info.channels != 1 && info.channels != 2) || info.hz <= 0) { free(cache); return NULL; }
        while (sample_pos < samples) {
            int source = (int)sample_pos;
            int mixed = info.channels == 2
                ? ((int)frame[source * 2] + (int)frame[source * 2 + 1]) / 2
                : frame[source];
            if (length == INT_MAX || ((size_t)length == capacity && !s_mp3_reserve(&cache, &capacity, length + 1))) {
                free(cache);
                return NULL;
            }
            cache->data[length++] = (short)mixed;
            sample_pos += (double)info.hz / 44100.0;
        }
        sample_pos -= samples;
    }
    if (!length) { free(cache); return NULL; }
    cache->length = length;
    cache->loopstart = -1;
    return cache;
}
