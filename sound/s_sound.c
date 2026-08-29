/*
 * s_sound.c — Sound system modelled after Quake 2's snd_dma/snd_mem.
 *
 * DBC SoundEntries kits and raw file paths are the two handle types.
 * Both carry a sfxcache_t* that is NULL until first use (lazy load).
 * WAV parsing and resampling are done with Q2's GetWavinfo/ResampleSfx;
 * output is always S16, 44100 Hz, mono to match the SDL device spec.
 */
#include "s_local.h"
#include "games/world-of-warcraft/common/stb_dbc.h"

sState_t s;

/* =========================================================================
 * WAV parsing — verbatim from Quake 2 snd_mem.c
 * ========================================================================= */

typedef struct {
    int rate;
    int width;
    int channels;
    int loopstart;
    int samples;
    int dataofs;
} wavinfo_t;

static BYTE *data_p;
static BYTE *iff_end;
static BYTE *last_chunk;
static BYTE *iff_data;
static int   iff_chunk_len;

static short GetLittleShort(void) {
    short val = *data_p | (*(data_p + 1) << 8);
    data_p += 2;
    return val;
}

static int GetLittleLong(void) {
    int val = data_p[0] | (data_p[1] << 8) | (data_p[2] << 16) | (data_p[3] << 24);
    data_p += 4;
    return val;
}

static void FindNextChunk(const char *name) {
    while (1) {
        data_p = last_chunk;
        if (data_p >= iff_end) { data_p = NULL; return; }
        data_p += 4;
        iff_chunk_len = GetLittleLong();
        if (iff_chunk_len < 0) { data_p = NULL; return; }
        data_p -= 8;
        last_chunk = data_p + 8 + ((iff_chunk_len + 1) & ~1);
        if (!strncmp((char *)data_p, name, 4)) return;
    }
}

static void FindChunk(const char *name) {
    last_chunk = iff_data;
    FindNextChunk(name);
}

static wavinfo_t GetWavinfo(const char *name, BYTE *wav, int wavlength) {
    wavinfo_t info;
    memset(&info, 0, sizeof(info));
    if (!wav) return info;

    iff_data = wav;
    iff_end  = wav + wavlength;

    FindChunk("RIFF");
    if (!(data_p && !strncmp((char *)data_p + 8, "WAVE", 4))) {
        fprintf(stderr, "[sound] %s: missing RIFF/WAVE\n", name);
        return info;
    }
    iff_data = data_p + 12;

    FindChunk("fmt ");
    if (!data_p) { fprintf(stderr, "[sound] %s: missing fmt chunk\n", name); return info; }
    data_p += 8;
    if (GetLittleShort() != 1) {
        fprintf(stderr, "[sound] %s: not PCM format\n", name);
        return info;
    }
    info.channels = GetLittleShort();
    info.rate     = GetLittleLong();
    data_p += 6;
    info.width    = GetLittleShort() / 8;

    FindChunk("cue ");
    if (data_p) {
        data_p += 32;
        info.loopstart = GetLittleLong();
        FindNextChunk("LIST");
        if (data_p && !strncmp((char *)data_p + 28, "mark", 4)) {
            data_p += 24;
            int loop_len = GetLittleLong();
            info.samples = info.loopstart + loop_len;
        }
    } else {
        info.loopstart = -1;
    }

    FindChunk("data");
    if (!data_p) { fprintf(stderr, "[sound] %s: missing data chunk\n", name); return info; }
    data_p += 4;
    /* Always use the data chunk size for total length. The cue/LIST loop markers
     * in WC3 files describe a loop region inside a longer sound, not the full
     * duration — using loopstart+loop_len here would crop the audio. */
    info.samples = GetLittleLong() / info.width;

    info.dataofs = (int)(data_p - wav);
    return info;
}

/* =========================================================================
 * Resample + allocate sfxcache_t (mirrors Q2 ResampleSfx, always → S16/44100/mono)
 * ========================================================================= */

static sfxcache_t *S_ResampleLoad(const char *path) {
    DWORD file_size = 0;
    BYTE *file_data = FS_ReadFile(path, &file_size);
    if (!file_data || !file_size) {
        FS_FreeFile(file_data);
        return NULL;
    }

    wavinfo_t info = GetWavinfo(path, file_data, (int)file_size);
    if (info.channels != 1) {
        fprintf(stderr, "[sound] %s: %d-channel audio, rejecting (need mono)\n", path, info.channels);
        FS_FreeFile(file_data);
        return NULL;
    }
    if (!info.samples || !info.width) {
        fprintf(stderr, "[sound] %s: bad WAV (samples=%d width=%d)\n", path, info.samples, info.width);
        FS_FreeFile(file_data);
        return NULL;
    }

    float stepscale = (float)info.rate / 44100.0f;
    int   outcount  = (int)((float)info.samples / stepscale);
    if (outcount <= 0) { FS_FreeFile(file_data); return NULL; }

    sfxcache_t *sc = malloc(sizeof(sfxcache_t) + (outcount - 1) * sizeof(short));
    if (!sc) { FS_FreeFile(file_data); return NULL; }
    sc->length    = outcount;
    sc->loopstart = (info.loopstart != -1) ? (int)((float)info.loopstart / stepscale) : -1;

    BYTE *src      = file_data + info.dataofs;
    int   fracstep = (int)(stepscale * 256.0f);
    int   samplefrac = 0;
    for (int i = 0; i < outcount; i++) {
        int srcsample = samplefrac >> 8;
        samplefrac += fracstep;
        int sample;
        if (info.width == 2)
            sample = (int)(short)(src[srcsample * 2] | (src[srcsample * 2 + 1] << 8));
        else
            sample = ((int)(unsigned char)src[srcsample] - 128) << 8;
        sc->data[i] = (short)sample;
    }

    FS_FreeFile(file_data);
    return sc;
}

/* =========================================================================
 * Name hash (for DBC kit lookup by name)
 * ========================================================================= */

static DWORD S_HashString(LPCSTR str) {
    DWORD hash = 5381;
    for (; *str; str++) hash = ((hash << 5) + hash) + (unsigned char)*str;
    return hash & (S_HASH_BUCKETS - 1);
}

static sHashNode_t *S_FindByName(LPCSTR name) {
    DWORD bucket = S_HashString(name);
    for (sHashNode_t *n = s.hash_buckets[bucket]; n; n = n->next)
        if (n->kit_id && s.kits[n->kit_id].name && !strcasecmp(s.kits[n->kit_id].name, name))
            return n;
    return NULL;
}

static void S_InsertHash(DWORD kit_id, LPCSTR name) {
    if (!name || !*name) return;
    DWORD bucket = S_HashString(name);
    sHashNode_t *n = &s.hash_pool[s.hash_pool_used++];
    n->kit_id = kit_id;
    n->next   = s.hash_buckets[bucket];
    s.hash_buckets[bucket] = n;
}

/* =========================================================================
 * Load cache — mirrors Q2's S_LoadSound
 * ========================================================================= */

/* Load (or return cached) PCM for a kit entry. */
static sfxcache_t *S_LoadKit(sSoundKit_t *k) {
    if (!k || k->id == 0 || !k->files[0] || !*k->files[0]) return NULL;
    if (k->cache) return k->cache;

    char path[512];
    if (k->directoryBase && *k->directoryBase && *k->directoryBase != '(')
        snprintf(path, sizeof(path), "%s\\%s", k->directoryBase, k->files[0]);
    else
        snprintf(path, sizeof(path), "%s", k->files[0]);

    k->cache = S_ResampleLoad(path);
    return k->cache;
}

/* Load (or return cached) PCM for a path-keyed sfx handle. */
static sfxcache_t *S_LoadSfx(sfx_t *sfx) {
    if (!sfx || !sfx->path[0]) return NULL;
    if (sfx->cache) return sfx->cache;
    sfx->cache = S_ResampleLoad(sfx->path);
    return sfx->cache;
}

/* Find or create a path-keyed sfx handle (mirrors Q2 S_FindName). */
static sfx_t *S_FindSfx(LPCSTR path, BOOL create) {
    for (int i = 0; i < s.num_sfx; i++)
        if (!strcasecmp(s.known_sfx[i].path, path))
            return &s.known_sfx[i];
    if (!create) return NULL;
    if (s.num_sfx >= S_MAX_SFX) {
        fprintf(stderr, "[sound] S_FindSfx: out of sfx slots\n");
        return NULL;
    }
    sfx_t *sfx = &s.known_sfx[s.num_sfx++];
    memset(sfx, 0, sizeof(*sfx));
    strncpy(sfx->path, path, sizeof(sfx->path) - 1);
    sfx->registration_sequence = s.registration_sequence;
    return sfx;
}

/* =========================================================================
 * DBC SoundEntries loader
 * ========================================================================= */

void S_LoadSoundEntries(void) {
    stbDbc_t h;
    DWORD size = 0;
    BYTE *data = FS_ReadFile("DBFilesClient\\SoundEntries.dbc", &size);
    if (!Stb_DbcValid(data, (DWORD)size, &h) ||
        h.fields != SENTRY_FIELDS || h.record_size != SENTRY_RECORD_SIZE) {
        FS_FreeFile(data);
        return;
    }
    BYTE *records = data + 20;
    BYTE *strings = records + h.records * h.record_size;

    for (DWORD i = 0; i < h.records && i < S_MAX_KITS; i++) {
        BYTE *rec = records + i * h.record_size;
        DWORD id = Stb_DbcField(&h, rec, 0);
        if (id == 0 || id >= S_MAX_KITS) continue;
        sSoundKit_t *k = &s.kits[id];
        k->id   = id;
        k->type = Stb_DbcField(&h, rec, 1);
        k->name = Stb_DbcString(strings, h.string_size, Stb_DbcField(&h, rec, 2));
        for (DWORD j = 0; j < SENTRY_MAX_FILES; j++)
            k->files[j] = Stb_DbcString(strings, h.string_size, Stb_DbcField(&h, rec, 3 + j));
        for (DWORD j = 0; j < SENTRY_MAX_FILES; j++)
            k->freq[j] = Stb_DbcField(&h, rec, 13 + j);
        k->directoryBase = Stb_DbcString(strings, h.string_size, Stb_DbcField(&h, rec, 23));
        k->volume = Stb_DbcReadFloat(rec + 24 * sizeof(DWORD));
        k->flags  = Stb_DbcField(&h, rec, 25);
        k->cache  = NULL;
        k->registration_sequence = s.registration_sequence;
        if (k->id >= s.kit_count) s.kit_count = k->id + 1;
        S_InsertHash(id, k->name);
    }
    s.dbc_data = data;
}

/* =========================================================================
 * Registration (mirrors Q2 S_BeginRegistration / S_EndRegistration)
 * ========================================================================= */

void S_BeginRegistration(void) {
    s.registration_sequence++;
}

void S_EndRegistration(void) {
    /* Free path-keyed sfx not used in this registration sequence */
    for (int i = 0; i < s.num_sfx; i++) {
        sfx_t *sfx = &s.known_sfx[i];
        if (!sfx->path[0]) continue;
        if (sfx->registration_sequence != s.registration_sequence) {
            free(sfx->cache);
            memset(sfx, 0, sizeof(*sfx));
        }
    }
    /* Compact the sfx table */
    int dst = 0;
    for (int i = 0; i < s.num_sfx; i++) {
        if (s.known_sfx[i].path[0])
            s.known_sfx[dst++] = s.known_sfx[i];
    }
    s.num_sfx = dst;

    /* Free kit caches not touched this sequence */
    for (DWORD i = 1; i < s.kit_count; i++) {
        sSoundKit_t *k = &s.kits[i];
        if (k->id != i) continue;
        if (k->registration_sequence != s.registration_sequence && k->cache) {
            free(k->cache);
            k->cache = NULL;
        }
    }
}

/* =========================================================================
 * Stop all sounds (mirrors Q2 S_StopAllSounds)
 * ========================================================================= */

void S_StopAllSounds(void) {
    if (!s.initialized) return;
    SDL_LockAudioDevice(s.device);
    memset(s.channels, 0, sizeof(s.channels));
    SDL_UnlockAudioDevice(s.device);
}

/* =========================================================================
 * SDL audio mixer callback
 * ========================================================================= */

static void SDLCALL S_MixAudio(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    memset(stream, 0, len);
    Sint16 *out     = (Sint16 *)stream;
    int     samples = len / (int)sizeof(Sint16);

    for (int ch = 0; ch < S_MAX_CHANNELS; ch++) {
        if (!s.channels[ch].active || !s.channels[ch].sc) continue;
        sfxcache_t *sc  = s.channels[ch].sc;
        float       vol = s.channels[ch].volume;
        int         pos = s.channels[ch].pos;

        for (int i = 0; i < samples; i++) {
            if (pos >= sc->length) {
                s.channels[ch].active = FALSE;
                break;
            }
            int mixed = (int)out[i] + (int)(sc->data[pos] * vol);
            if (mixed >  32767) mixed =  32767;
            if (mixed < -32768) mixed = -32768;
            out[i] = (Sint16)mixed;
            pos++;
        }
        s.channels[ch].pos = pos;
    }
}

/* =========================================================================
 * Init / Shutdown
 * ========================================================================= */

BOOL S_Init(void) {
    memset(&s, 0, sizeof(s));
    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "[sound] SDL_Init: %s\n", SDL_GetError());
        return FALSE;
    }
    SDL_AudioSpec want = {0}, have = {0};
    want.freq     = 44100;
    want.format   = AUDIO_S16SYS;
    want.channels = 1;
    want.samples  = 1024;
    want.callback = S_MixAudio;
    s.device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (s.device == 0) {
        fprintf(stderr, "[sound] SDL_OpenAudioDevice: %s\n", SDL_GetError());
        return FALSE;
    }
    SDL_PauseAudioDevice(s.device, 0);
    S_LoadSoundEntries();
    s.initialized = TRUE;
    return TRUE;
}

void S_Shutdown(void) {
    if (!s.initialized) return;
    S_StopAllSounds();
    SDL_CloseAudioDevice(s.device);
    for (int i = 0; i < s.num_sfx; i++)
        free(s.known_sfx[i].cache);
    for (DWORD i = 1; i < s.kit_count; i++)
        if (s.kits[i].id == i) free(s.kits[i].cache);
    FS_FreeFile(s.dbc_data);
    memset(&s, 0, sizeof(s));
}

/* =========================================================================
 * Playback helpers
 * ========================================================================= */

static void S_StartSound(sfxcache_t *sc, float volume) {
    if (!sc) return;
    SDL_LockAudioDevice(s.device);
    for (int ch = 0; ch < S_MAX_CHANNELS; ch++) {
        if (!s.channels[ch].active) {
            s.channels[ch].sc     = sc;
            s.channels[ch].pos    = 0;
            s.channels[ch].volume = volume;
            s.channels[ch].active = TRUE;
            SDL_UnlockAudioDevice(s.device);
            return;
        }
    }
    SDL_UnlockAudioDevice(s.device);
}

/* =========================================================================
 * Public API
 * ========================================================================= */

void S_PlaySound(DWORD kit_id) {
    if (!s.initialized || kit_id == 0 || kit_id >= S_MAX_KITS) return;
    sSoundKit_t *k = &s.kits[kit_id];
    if (k->id != kit_id) return;
    k->registration_sequence = s.registration_sequence;
    S_StartSound(S_LoadKit(k), k->volume > 0.0f ? k->volume : 1.0f);
}

void S_PlaySoundByName(LPCSTR name) {
    if (!s.initialized || !name || !*name) return;
    sHashNode_t *n = S_FindByName(name);
    if (n) S_PlaySound(n->kit_id);
}

/* Preload a server-configstring sound so playback never blocks on archive I/O. */
void S_RegisterSound(LPCSTR path) {
    if (!s.initialized || !path || !*path) return;
    sfx_t *sfx = S_FindSfx(path, TRUE);
    if (!sfx) return;
    sfx->registration_sequence = s.registration_sequence;
    S_LoadSfx(sfx);
}

/* Play a sound by raw MPQ-relative path (e.g. unit voice lines). */
void S_PlaySoundFile(LPCSTR path) {
    if (!s.initialized || !path || !*path) return;
    sfx_t *sfx = S_FindSfx(path, TRUE);
    if (!sfx) return;
    sfx->registration_sequence = s.registration_sequence;
    S_StartSound(S_LoadSfx(sfx), 1.0f);
}
