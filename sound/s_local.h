#ifndef s_local_h
#define s_local_h

#include "common/common.h"
#include <SDL2/SDL.h>

/* SoundEntries.dbc field layout (classic WoW, 29 fields, 116 bytes/record):
   0=ID 1=type 2=name(string) 3-12=file[0..9](strings)
   13-22=freq[0..9] 23=directoryBase(string) 24=volumeFloat
   25=flags 26=minDistance 27=distanceCutoff 28=eaxdef 29=advancedID */
#define SENTRY_FIELDS     29
#define SENTRY_RECORD_SIZE 116
#define SENTRY_MAX_FILES  10

#define S_MAX_KITS        8192
#define S_MAX_CHANNELS    8
#define S_HASH_BUCKETS    256
#define S_MAX_CACHED_WAVS 64

typedef struct {
    DWORD id;
    DWORD type;
    LPCSTR name;           /* points into DBC string block */
    LPCSTR files[SENTRY_MAX_FILES];
    DWORD freq[SENTRY_MAX_FILES];
    LPCSTR directoryBase;  /* points into DBC string block */
    float volume;
    DWORD flags;
} sSoundKit_t;

typedef struct {
    Uint8 *data;
    Uint32 len;
    SDL_AudioSpec spec;
    DWORD kit_id;
    LPCSTR path;  /* non-NULL for path-keyed entries (kit_id==0) */
} sWavCache_t;

typedef struct sHashNode_s {
    DWORD kit_id;
    struct sHashNode_s *next;
} sHashNode_t;

typedef struct {
    sSoundKit_t kits[S_MAX_KITS];
    DWORD kit_count;
    sHashNode_t *hash_buckets[S_HASH_BUCKETS];
    sHashNode_t hash_pool[S_MAX_KITS];
    DWORD hash_pool_used;

    sWavCache_t wav_cache[S_MAX_CACHED_WAVS];
    DWORD wav_cache_lru[S_MAX_CACHED_WAVS];
    DWORD wav_cache_count;

    /* Active playback channels */
    struct {
        sWavCache_t *wav;
        Uint32 pos;
        float volume;
        BOOL active;
    } channels[S_MAX_CHANNELS];

    SDL_AudioDeviceID device;
    BOOL initialized;
    BYTE *dbc_data;
} sState_t;

extern sState_t s;

/* s_sound.c */
void S_LoadSoundEntries(void);
void S_PlaySoundFile(LPCSTR path);

#endif
