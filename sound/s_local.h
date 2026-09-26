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
#define S_MAX_CHANNELS      24
#define S_HASH_BUCKETS      256

typedef enum {
    S_STREAM_MOVIE = 0,
    S_STREAM_MUSIC,
    S_STREAM_COUNT
} sStreamId_t;

typedef struct {
    short *data;
    uint32_t capacity; /* stereo frames */
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t count;
    uint64_t played_frames; /* frames consumed by the device since S_StreamStart */
    float volume;
    bool active;
    bool paused;
} sStreamState_t;

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
    int          load_attempt_sequence;
    bool         load_attempted;
} sfx_t;

/* DBC kit entry — cache pointer added so the decoded PCM lives on the handle. */
typedef struct {
    uint32_t        id;
    uint32_t        type;
    cstring_t       name;
    cstring_t       files[SENTRY_MAX_FILES];
    uint32_t        freq[SENTRY_MAX_FILES];
    cstring_t       directoryBase;
    float        volume;
    uint32_t        flags;
    sfxcache_t  *cache;
    int          registration_sequence;
    int          load_attempt_sequence;
    bool         load_attempted;
} sSoundKit_t;

typedef struct sHashNode_s {
    uint32_t kit_id;
    struct sHashNode_s *next;
} sHashNode_t;

typedef struct {
    /* DBC kit table */
    sSoundKit_t  kits[S_MAX_KITS];
    uint32_t        kit_count;
    sHashNode_t *hash_buckets[S_HASH_BUCKETS];
    sHashNode_t  hash_pool[S_MAX_KITS];
    uint32_t        hash_pool_used;

    /* Path-keyed sfx table (mirrors Q2 known_sfx[]) */
    sfx_t        known_sfx[S_MAX_SFX];
    int          num_sfx;

    /* Registration sequence — bump on map load to free stale caches */
    int          registration_sequence;

    /* Listener state for spatialization — set each frame from the camera */
    struct {
        vec2_t origin;
        vec2_t right;   /* normalized right vector in world XY */
    } listener;

    /* Active playback channels */
    struct {
        sfxcache_t *sc;
        int         pos;
        float       master_vol;
        float       leftvol;
        float       rightvol;
        vec2_t     origin;
        float       attenuation;
        int         channel;
        unsigned    priority;
        soundPolicy_t policy;
        uint64_t serial;
        uint32_t started;
        int         delay;
        uint32_t       entity;
        uint32_t       loop_generation;
        bool        looping;
        bool        is_positional;
        bool        active, notified_start;
    } channels[S_MAX_CHANNELS];
    uint32_t loop_generation;
    uint64_t sound_serial;
    struct { uint32_t end; bool active; } user_cooldown[MAX_GAME_ENTITIES];

    /* Client-owned long-form PCM streams: stereo S16 at the mixer rate.
     * Keep movie and music lifetime independent so one presentation source
     * cannot reset the other's decoder buffer. */
    sStreamState_t streams[S_STREAM_COUNT];

    SDL_AudioDeviceID device;
    bool              initialized;
    uint8_t             *dbc_data;
} sState_t;

extern sState_t s;

/* s_sound.c */
sfxcache_t *s_mp3_decode(uint8_t const *data, uint32_t size);
void S_LoadSoundEntries(void);
void S_BeginRegistration(void);
void S_EndRegistration(void);
void S_RegisterSound(cstring_t path);
void S_PlaySoundFile(cstring_t path);
void S_PlaySoundAt(cstring_t path, vec2_t const *origin);
void S_PlaySoundPacket(cstring_t path, vec3_t const *origin, bool positioned, int channel, float volume, float attenuation,
                       float timeofs);
bool S_PollSoundEvent(soundEvent_t *event);
void S_ClearSoundEvents(void);
bool S_PlaySoundPolicy(cstring_t path, vec3_t const *origin, bool positioned, int channel, float volume, float attenuation, float timeofs, soundPolicy_t const *policy);
#ifdef BZ_TESTS
void S_TestMix(int16_t *out, uint32_t frames);
#endif
void S_BeginLoopingSounds(void);
void S_UpdateLoopingSound(uint32_t entity, cstring_t path, vec2_t const *origin, float volume, float attenuation);
void S_EndLoopingSounds(void);
void S_SetListener(vec2_t const *origin, vec2_t const *right);
void S_StreamStart(sStreamId_t stream);
uint32_t S_StreamSamples(sStreamId_t stream, int16_t const *samples, uint32_t frames);
uint32_t S_StreamBufferedFrames(sStreamId_t stream);
uint64_t S_StreamPlayedFrames(sStreamId_t stream);
void S_StreamSetVolume(sStreamId_t stream, float volume);
void S_StreamSetPaused(sStreamId_t stream, bool paused);
void S_StreamStop(sStreamId_t stream);

#endif
