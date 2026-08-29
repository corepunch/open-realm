#ifndef s_local_h
#define s_local_h

#include "common/common.h"
#include <SDL2/SDL.h>

/* SoundEntries.dbc field layout (classic WoW, 29 fields, 116 bytes/record):
   0=ID 1=type 2=name(string) 3-12=file[0..9](strings)
   13-22=freq[0..9] 23=directoryBase(string) 24=volumeFloat
   25=flags 26=minDistance 27=distanceCutoff 28=eaxdef 29=advancedID */
#define SENTRY_FIELDS       29
#define SENTRY_RECORD_SIZE  116
#define SENTRY_MAX_FILES    10

#define S_MAX_KITS          8192
#define S_MAX_SFX           512
#define S_MAX_CHANNELS      8
#define S_HASH_BUCKETS      256

/* Decoded PCM cache entry — always S16, 44100 Hz, mono (mirrors Q2 sfxcache_t).
 * Allocated as: malloc(sizeof(sfxcache_t) + length * sizeof(short)) */
typedef struct {
    int   length;    /* sample count */
    int   loopstart; /* -1 = no loop */
    short data[1];   /* S16 samples at 44100 Hz mono */
} sfxcache_t;

/* Path-keyed sound handle (mirrors Q2 sfx_t). */
typedef struct {
    char         path[512];
    sfxcache_t  *cache;
    int          registration_sequence;
} sfx_t;

/* DBC kit entry — cache pointer added so the decoded PCM lives on the handle. */
typedef struct {
    DWORD        id;
    DWORD        type;
    LPCSTR       name;
    LPCSTR       files[SENTRY_MAX_FILES];
    DWORD        freq[SENTRY_MAX_FILES];
    LPCSTR       directoryBase;
    float        volume;
    DWORD        flags;
    sfxcache_t  *cache;
    int          registration_sequence;
} sSoundKit_t;

typedef struct sHashNode_s {
    DWORD kit_id;
    struct sHashNode_s *next;
} sHashNode_t;

typedef struct {
    /* DBC kit table */
    sSoundKit_t  kits[S_MAX_KITS];
    DWORD        kit_count;
    sHashNode_t *hash_buckets[S_HASH_BUCKETS];
    sHashNode_t  hash_pool[S_MAX_KITS];
    DWORD        hash_pool_used;

    /* Path-keyed sfx table (mirrors Q2 known_sfx[]) */
    sfx_t        known_sfx[S_MAX_SFX];
    int          num_sfx;

    /* Registration sequence — bump on map load to free stale caches */
    int          registration_sequence;

    /* Active playback channels */
    struct {
        sfxcache_t *sc;
        int         pos;
        float       volume;
        BOOL        active;
    } channels[S_MAX_CHANNELS];

    SDL_AudioDeviceID device;
    BOOL              initialized;
    BYTE             *dbc_data;
} sState_t;

extern sState_t s;

/* s_sound.c */
void S_LoadSoundEntries(void);
void S_BeginRegistration(void);
void S_EndRegistration(void);
void S_PlaySoundFile(LPCSTR path);

#endif
