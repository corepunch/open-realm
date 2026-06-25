#ifndef s_local_h
#define s_local_h

#include "common/common.h"
#include <SDL2/SDL.h>

/* SoundEntries.dbc field layout (classic WoW, 29 fields, 116 bytes/record):
   0=ID 1=type 2=name(string) 3-12=file[0..9](strings)
   13-22=freq[0..9] 23=directoryBase(string) 24=volumeFloat
   25=flags 26=minDistance 27=distanceCutoff 28=eaxdef 29=advancedID */
#define SENTRY_FIELDS      29
#define SENTRY_RECORD_SIZE 116
#define SENTRY_MAX_FILES   10

#define S_MAX_KITS         8192
#define S_MAX_CHANNELS     16
#define S_HASH_BUCKETS     256
#define S_MAX_CACHED_WAVS  64
#define S_MAX_PLAYSOUNDS   128

/* Distance before attenuation begins (Quake 2 uses 80). */
#define SOUND_FULLVOLUME   80

/* ---- Sound kit from DBC ---- */
typedef struct {
    DWORD id;
    DWORD type;
    LPCSTR name;
    LPCSTR files[SENTRY_MAX_FILES];
    DWORD freq[SENTRY_MAX_FILES];
    LPCSTR directoryBase;
    float volume;
    DWORD flags;
} sSoundKit_t;

/* ---- Cached decoded WAV ---- */
typedef struct {
    Uint8 *data;
    Uint32 len;
    SDL_AudioSpec spec;
    DWORD kit_id;
} sWavCache_t;

/* ---- Hash lookup node ---- */
typedef struct sHashNode_s {
    DWORD kit_id;
    struct sHashNode_s *next;
} sHashNode_t;

/* ---- Pending one-shot sound (Quake 2 style) ---- */
typedef struct playsound_s {
    struct playsound_s *prev, *next;
    sSoundKit_t *kit;
    sWavCache_t *wav;
    DWORD entnum;
    DWORD entchannel;
    float volume;
    DWORD begin;              /* sample time when this should start */
} playsound_t;

/* ---- Active playback channel ---- */
typedef struct {
    sWavCache_t *wav;
    int pos;
    int end;                  /* painted time when sound finishes */
    DWORD entnum;
    DWORD entchannel;
    int leftvol, rightvol;    /* 0-255 per ear */
    BOOL active;
} sChannel_t;

/* ---- Global sound state ---- */
typedef struct {
    sSoundKit_t kits[S_MAX_KITS];
    DWORD kit_count;
    sHashNode_t *hash_buckets[S_HASH_BUCKETS];
    sHashNode_t hash_pool[S_MAX_KITS];
    DWORD hash_pool_used;

    sWavCache_t wav_cache[S_MAX_CACHED_WAVS];
    DWORD wav_cache_lru[S_MAX_CACHED_WAVS];
    DWORD wav_cache_count;

    sChannel_t channels[S_MAX_CHANNELS];

    /* Playsound queue (doubly-linked, sorted by begin time) */
    playsound_t playsound_pool[S_MAX_PLAYSOUNDS];
    playsound_t free_playsounds;   /* sentinel */
    playsound_t pending_playsounds;/* sentinel */

    /* Pre-mixed buffer written by main thread, read by SDL callback */
    Sint16 *mix_buf;
    DWORD mix_buf_samples;         /* total samples in ring buffer */
    DWORD mix_buf_read;            /* callback reads here */
    DWORD mix_buf_write;           /* main thread writes here */
    DWORD painted;                 /* total samples painted so far */

    /* Listener position — set by client each frame */
    VECTOR3 listener_origin;

    SDL_AudioDeviceID device;
    BOOL initialized;
    BYTE *dbc_data;
    DWORD local_entnum;            /* entity number of local player (0 = none) */
} sState_t;

extern sState_t s;

/* s_sound.c */
void S_LoadSoundEntries(void);
void S_Update(void);
BOOL S_Init(void);
void S_Shutdown(void);
void S_PlaySound(DWORD kit_id);
void S_PlaySoundByName(LPCSTR name);
void S_SetListener(LPCVECTOR3 origin);

#endif
