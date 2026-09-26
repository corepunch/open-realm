/*
 * s_sound.c — Sound system modelled after Quake 2's snd_dma/snd_mem.
 *
 * DBC SoundEntries kits and raw file paths are the two handle types.
 * Both carry a sfxcache_t* that is NULL until first use (lazy load).
 * WAV parsing and resampling follow Q2's GetWavinfo/ResampleSfx; minimp3
 * decodes MP3 dialogue into the same S16, 44100 Hz, mono cache format.
 */
#include "s_local.h"
#include "common/stb_dbc.h"

sState_t s;

/* Main thread reserves room for every active channel's remaining notifications.
 * The audio callback only appends; it never allocates or calls game/network code. */
static soundEvent_t *sound_events;
static size_t sound_event_read, sound_event_count, sound_event_capacity;
static void S_EndChannel(int ch);

static void S_ReserveSoundEvents(void) {
    SDL_LockAudioDevice(s.device);
    if (sound_event_read) {
        memmove(sound_events, sound_events + sound_event_read,
                (sound_event_count - sound_event_read) * sizeof(*sound_events));
        sound_event_count -= sound_event_read; sound_event_read = 0;
    }
    size_t need = sound_event_count + 2 * S_MAX_CHANNELS + 3;
    if (need > sound_event_capacity) {
        size_t capacity = MAX(need, sound_event_capacity * 2);
        void *events = realloc(sound_events, capacity * sizeof(*sound_events));
        if (!events) { fprintf(stderr, "S_ReserveSoundEvents: out of memory\n"); abort(); }
        sound_events = events; sound_event_capacity = capacity;
    }
    SDL_UnlockAudioDevice(s.device);
}

static void S_SoundEvent(soundPolicy_t const *policy, uint32_t event) {
    if (!policy || !policy->request) return;
    if (sound_event_count == sound_event_capacity) {
        fprintf(stderr, "S_SoundEvent: missing notification reservation\n"); abort();
    }
    sound_events[sound_event_count++] = (soundEvent_t){policy->user, policy->request, event};
}

bool S_PollSoundEvent(soundEvent_t *event) {
    SDL_LockAudioDevice(s.device);
    bool found = sound_event_read < sound_event_count;
    if (found) *event = sound_events[sound_event_read++];
    if (sound_event_read == sound_event_count) sound_event_read = sound_event_count = 0;
    SDL_UnlockAudioDevice(s.device);
    return found;
}

void S_ClearSoundEvents(void) {
    SDL_LockAudioDevice(s.device);
    sound_event_read = sound_event_count = 0;
    SDL_UnlockAudioDevice(s.device);
}

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

static uint8_t *data_p;
static uint8_t *iff_end;
static uint8_t *last_chunk;
static uint8_t *iff_data;
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

static wavinfo_t GetWavinfo(const char *name, uint8_t *wav, int wavlength) {
    wavinfo_t info;
    memset(&info, 0, sizeof(info));
    if (!wav) return info;

    iff_data = wav;
    iff_end  = wav + wavlength;

    FindChunk("RIFF");
    if (!(data_p && !strncmp((char *)data_p + 8, "WAVE", 4))) {
        /* Format dispatch handles non-WAV audio before calling this parser. */
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
    int data_bytes = GetLittleLong();
    if (info.width > 0 && info.channels > 0)
        info.samples = data_bytes / (info.width * info.channels);

    info.dataofs = (int)(data_p - wav);
    return info;
}

/* =========================================================================
 * Resample + allocate sfxcache_t (mirrors Q2 ResampleSfx, always → S16/44100/mono)
 * ========================================================================= */

static bool s_is_mp3_path(cstring_t path) {
    cstring_t extension = path ? strrchr(path, '.') : NULL;
    return extension && !strcasecmp(extension, ".mp3");
}

static sfxcache_t *S_ResampleLoad(const char *path) {
    uint32_t file_size = 0;
    uint8_t *file_data = FS_ReadFile(path, &file_size);
    if (!file_data || !file_size) {
        fprintf(stderr, "[sound] %s: failed to read audio file\n", path);
        FS_FreeFile(file_data);
        return NULL;
    }

    if (file_size < 12 || strncmp((char *)file_data, "RIFF", 4) || strncmp((char *)file_data + 8, "WAVE", 4)) {
        bool is_mp3 = s_is_mp3_path(path);
        sfxcache_t *sc = is_mp3 ? s_mp3_decode(file_data, file_size) : NULL;
        if (!sc) fprintf(stderr, "[sound] %s: %s\n", path,
                         is_mp3 ? "MP3 decode failed" : "unsupported audio format (expected WAV or MP3)");
        FS_FreeFile(file_data);
        return sc;
    }

    wavinfo_t info = GetWavinfo(path, file_data, (int)file_size);
    if (info.channels != 1 && info.channels != 2) {
        fprintf(stderr, "[sound] %s: unsupported WAV channels=%d\n", path, info.channels);
        FS_FreeFile(file_data);
        return NULL;
    }
    if (!info.samples || (info.width != 1 && info.width != 2)) {
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

    uint8_t *src      = file_data + info.dataofs;
    int   fracstep = (int)(stepscale * 256.0f);
    int   samplefrac = 0;
    for (int i = 0; i < outcount; i++) {
        int srcsample = samplefrac >> 8;
        samplefrac += fracstep;
        int sample = 0;
        for (int channel = 0; channel < info.channels; channel++) {
            uint8_t *pcm = src + (srcsample * info.channels + channel) * info.width;
            sample += info.width == 2 ? (short)(pcm[0] | (pcm[1] << 8))
                                      : ((int)pcm[0] - 128) << 8;
        }
        sc->data[i] = (short)(sample / info.channels);
    }

    FS_FreeFile(file_data);
    return sc;
}

/* =========================================================================
 * Name hash (for DBC kit lookup by name)
 * ========================================================================= */

static uint32_t S_HashString(cstring_t str) {
    uint32_t hash = 5381;
    for (; *str; str++) hash = ((hash << 5) + hash) + (unsigned char)*str;
    return hash & (S_HASH_BUCKETS - 1);
}

static sHashNode_t *S_FindByName(cstring_t name) {
    uint32_t bucket = S_HashString(name);
    for (sHashNode_t *n = s.hash_buckets[bucket]; n; n = n->next)
        if (n->kit_id && s.kits[n->kit_id].name && !strcasecmp(s.kits[n->kit_id].name, name))
            return n;
    return NULL;
}

static void S_InsertHash(uint32_t kit_id, cstring_t name) {
    if (!name || !*name) return;
    uint32_t bucket = S_HashString(name);
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
    if (k->load_attempted && k->load_attempt_sequence == s.registration_sequence) return NULL;
    k->load_attempted = true;
    k->load_attempt_sequence = s.registration_sequence;

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
    if (sfx->load_attempted && sfx->load_attempt_sequence == s.registration_sequence) return NULL;
    sfx->load_attempted = true;
    sfx->load_attempt_sequence = s.registration_sequence;
    sfx->cache = S_ResampleLoad(sfx->path);
    return sfx->cache;
}

/* Find or create a path-keyed sfx handle (mirrors Q2 S_FindName). */
static sfx_t *S_FindSfx(cstring_t path, bool create) {
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
    uint32_t size = 0;
    uint8_t *data = FS_ReadFile("DBFilesClient\\SoundEntries.dbc", &size);
    if (!Stb_DbcValid(data, (uint32_t)size, &h) ||
        h.fields != SENTRY_FIELDS || h.record_size != SENTRY_RECORD_SIZE) {
        FS_FreeFile(data);
        return;
    }
    uint8_t *records = data + 20;
    uint8_t *strings = records + h.records * h.record_size;

    for (uint32_t i = 0; i < h.records && i < S_MAX_KITS; i++) {
        uint8_t *rec = records + i * h.record_size;
        uint32_t id = Stb_DbcField(&h, rec, 0);
        if (id == 0 || id >= S_MAX_KITS) continue;
        sSoundKit_t *k = &s.kits[id];
        k->id   = id;
        k->type = Stb_DbcField(&h, rec, 1);
        k->name = Stb_DbcString(strings, h.string_size, Stb_DbcField(&h, rec, 2));
        for (uint32_t j = 0; j < SENTRY_MAX_FILES; j++)
            k->files[j] = Stb_DbcString(strings, h.string_size, Stb_DbcField(&h, rec, 3 + j));
        for (uint32_t j = 0; j < SENTRY_MAX_FILES; j++)
            k->freq[j] = Stb_DbcField(&h, rec, 13 + j);
        k->directoryBase = Stb_DbcString(strings, h.string_size, Stb_DbcField(&h, rec, 23));
        k->volume = Stb_DbcReadFloat(rec + 24 * sizeof(uint32_t));
        k->flags  = Stb_DbcField(&h, rec, 25);
        k->cache  = NULL;
        k->load_attempted = false;
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
    for (uint32_t i = 1; i < s.kit_count; i++) {
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
    FOR_LOOP(ch, S_MAX_CHANNELS) S_EndChannel(ch);
    memset(s.channels, 0, sizeof(s.channels));
    memset(s.user_cooldown, 0, sizeof(s.user_cooldown));
    SDL_UnlockAudioDevice(s.device);
}

/* =========================================================================
 * Spatialization
 * =========================================================================
 * Full volume within S_FULL_DIST world units; linear falloff to silence at
 * S_CUTOFF_DIST.  Stereo pan from dot-product of source direction vs the
 * listener's right vector (mirrors Quake 2 S_SpatializeOrigin). */

#define S_FULL_DIST   400.0f
#define S_CUTOFF_DIST 3000.0f

static void S_SpatializeChannel(int ch) {
    if (!s.channels[ch].is_positional) {
        s.channels[ch].leftvol = s.channels[ch].rightvol = s.channels[ch].master_vol;
        return;
    }
    float dx   = s.channels[ch].origin.x - s.listener.origin.x;
    float dy   = s.channels[ch].origin.y - s.listener.origin.y;
    float dist = sqrtf(dx * dx + dy * dy);

    if (dist >= S_CUTOFF_DIST) {
        s.channels[ch].leftvol = s.channels[ch].rightvol = 0.0f;
        return;
    }
    float att = (dist <= S_FULL_DIST)
        ? 1.0f
        : (S_CUTOFF_DIST - dist) / (S_CUTOFF_DIST - S_FULL_DIST);

    if (s.channels[ch].attenuation == 0.0f) att = 1.0f;
    else if (s.channels[ch].attenuation > 1.0f) att = powf(att, s.channels[ch].attenuation);
    float dot = 0.0f;
    if (dist > 1.0f)
        dot = (dx / dist) * s.listener.right.x + (dy / dist) * s.listener.right.y;

    s.channels[ch].leftvol  = s.channels[ch].master_vol * att * (0.5f * (1.0f - dot));
    s.channels[ch].rightvol = s.channels[ch].master_vol * att * (0.5f * (1.0f + dot));
}

/* =========================================================================
 * SDL audio mixer callback — stereo interleaved S16
 * ========================================================================= */

/* Device completion and admission preemption share the same lifetime path. */
static void S_EndChannel(int ch) {
    soundPolicy_t const *p = &s.channels[ch].policy;
    if (s.channels[ch].active && p->cooldown_ms && p->user < MAX_GAME_ENTITIES) {
        s.user_cooldown[p->user].end = SDL_GetTicks() + p->cooldown_ms;
        s.user_cooldown[p->user].active = true;
    }
    if (s.channels[ch].active) S_SoundEvent(p, SOUND_ENDED);
    s.channels[ch].active = false;
}

static void SDLCALL S_MixAudio(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    memset(stream, 0, len);
    Sint16 *out    = (Sint16 *)stream;
    int     frames = len / (int)(2 * sizeof(Sint16));  /* stereo frames */

    FOR_LOOP(stream_id, S_STREAM_COUNT) {
        sStreamState_t *stream_state = &s.streams[stream_id];
        if (!stream_state->active || stream_state->paused || !stream_state->data) continue;
        uint32_t take = MIN((uint32_t)frames, stream_state->count);
        for (uint32_t i = 0; i < take; i++) {
            uint32_t frame = stream_state->read_pos;
            int l = (int)out[i * 2] + (int)(stream_state->data[frame * 2] * stream_state->volume);
            int r = (int)out[i * 2 + 1] + (int)(stream_state->data[frame * 2 + 1] * stream_state->volume);
            if (l > 32767) l = 32767; else if (l < -32768) l = -32768;
            if (r > 32767) r = 32767; else if (r < -32768) r = -32768;
            out[i * 2] = (Sint16)l;
            out[i * 2 + 1] = (Sint16)r;
            stream_state->read_pos = (stream_state->read_pos + 1) % stream_state->capacity;
        }
        stream_state->count -= take;
        stream_state->played_frames += take;
    }

    for (int ch = 0; ch < S_MAX_CHANNELS; ch++) {
        if (!s.channels[ch].active || !s.channels[ch].sc) continue;
        S_SpatializeChannel(ch);
        sfxcache_t *sc   = s.channels[ch].sc;
        float       lvol = s.channels[ch].leftvol;
        float       rvol = s.channels[ch].rightvol;
        int         pos  = s.channels[ch].pos;
        int         skip = MIN(frames, s.channels[ch].delay);
        s.channels[ch].delay -= skip;
        if (skip == frames) continue;
        if (!s.channels[ch].notified_start) {
            S_SoundEvent(&s.channels[ch].policy, SOUND_STARTED);
            s.channels[ch].notified_start = true;
        }

        for (int i = skip; i < frames; i++) {
            if (pos >= sc->length) {
                if (s.channels[ch].looping && sc->length > 0) {
                    pos = sc->loopstart >= 0 && sc->loopstart < sc->length ? sc->loopstart : 0;
                } else {
                    S_EndChannel(ch);
                    break;
                }
            }
            int samp = (int)sc->data[pos++];
            int l = (int)out[i * 2]     + (int)(samp * lvol);
            int r = (int)out[i * 2 + 1] + (int)(samp * rvol);
            if (l >  32767) l =  32767; else if (l < -32768) l = -32768;
            if (r >  32767) r =  32767; else if (r < -32768) r = -32768;
            out[i * 2]     = (Sint16)l;
            out[i * 2 + 1] = (Sint16)r;
        }
        s.channels[ch].pos = pos;
        if (!s.channels[ch].looping && pos >= sc->length) S_EndChannel(ch);
    }
}

#ifdef BZ_TESTS
void S_TestMix(int16_t *out, uint32_t frames) { S_MixAudio(NULL, (Uint8 *)out, frames * 2 * sizeof(int16_t)); }
#endif

/* =========================================================================
 * Init / Shutdown
 * ========================================================================= */

bool S_Init(void) {
    memset(&s, 0, sizeof(s));
    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "[sound] SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    SDL_AudioSpec want = {0}, have = {0};
    want.freq     = 44100;
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = 1024;
    want.callback = S_MixAudio;
    s.device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (s.device == 0) {
        fprintf(stderr, "[sound] SDL_OpenAudioDevice: %s\n", SDL_GetError());
        return false;
    }
    SDL_PauseAudioDevice(s.device, 0);
    S_LoadSoundEntries();
    s.initialized = true;
    return true;
}

void S_Shutdown(void) {
    if (!s.initialized) return;
    S_StopAllSounds();
    SDL_CloseAudioDevice(s.device);
    for (int i = 0; i < s.num_sfx; i++)
        free(s.known_sfx[i].cache);
    for (uint32_t i = 1; i < s.kit_count; i++)
        if (s.kits[i].id == i) free(s.kits[i].cache);
    FS_FreeFile(s.dbc_data);
    FOR_LOOP(stream_id, S_STREAM_COUNT) free(s.streams[stream_id].data);
    memset(&s, 0, sizeof(s));
}

/* =========================================================================
 * Playback helpers
 * ========================================================================= */

/* Called under the device lock. List heads are the lowest-priority instances;
 * equal priorities insert at the head (newest first). Retail 1.27.1 admission: 6f0af5e0. */
static int S_AdmitSound(sfxcache_t *sc, soundPolicy_t const *p) {
    int free_slot = -1, count = 0, group_count = 0, duplicates = 0;
    int head = -1, oldest = -1, group_head = -1, group_oldest = -1, duplicate = -1;
    FOR_LOOP(i, S_MAX_CHANNELS) {
        if (!s.channels[i].active) { if (free_slot < 0) free_slot = i; continue; }
        count++;
        if (head < 0 || s.channels[i].priority < s.channels[head].priority ||
            (s.channels[i].priority == s.channels[head].priority &&
             (s.channels[i].policy.group < s.channels[head].policy.group ||
              (s.channels[i].policy.group == s.channels[head].policy.group && s.channels[i].serial > s.channels[head].serial)))) head = i;
        if (oldest < 0 || s.channels[i].serial < s.channels[oldest].serial) oldest = i;
        if (s.channels[i].sc == sc) {
            duplicates++;
            if (duplicate < 0 || s.channels[i].serial < s.channels[duplicate].serial) duplicate = i;
        }
        if (s.channels[i].policy.max_total && s.channels[i].policy.group == p->group) {
            group_count++;
            if (group_head < 0 || s.channels[i].priority < s.channels[group_head].priority ||
                (s.channels[i].priority == s.channels[group_head].priority && s.channels[i].serial > s.channels[group_head].serial)) group_head = i;
            if (s.channels[i].priority <= p->priority &&
                (group_oldest < 0 || s.channels[i].started < s.channels[group_oldest].started ||
                 (s.channels[i].started == s.channels[group_oldest].started &&
                  (s.channels[i].priority < s.channels[group_oldest].priority ||
                   (s.channels[i].priority == s.channels[group_oldest].priority && s.channels[i].serial > s.channels[group_oldest].serial))))) group_oldest = i;
        }
    }
    /* Duplicate identity preemption happens before capacity checks and stops
     * every matching instance, without a priority comparison. Zero is also an ID. */
    if (p->flags & SOUND_NO_DUPLICATE_USERS) {
        int user_slot = -1;
        FOR_LOOP(i, S_MAX_CHANNELS) if (s.channels[i].active && s.channels[i].policy.max_total &&
            !(s.channels[i].policy.flags & SOUND_IGNORE_USER) && s.channels[i].policy.user == p->user) {
            if (!(p->flags & SOUND_USER_PREEMPT)) return -1;
            if (user_slot < 0) user_slot = i;
        }
        if (user_slot >= 0) {
            FOR_LOOP(i, S_MAX_CHANNELS) if (s.channels[i].active && s.channels[i].policy.max_total &&
            !(s.channels[i].policy.flags & SOUND_IGNORE_USER) && s.channels[i].policy.user == p->user)
                S_EndChannel(i);
            return user_slot;
        }
    }
    if (duplicate >= 0 && (p->flags & SOUND_NO_DUPLICATES))
        return (p->flags & SOUND_DUPLICATE_PREEMPT) && s.channels[duplicate].priority < p->priority ? duplicate : -1;
    if (count >= p->max_total) {
        if (p->flags & SOUND_LIST_OLDEST) return oldest;
        return (p->flags & SOUND_LIST_PREEMPT) && head >= 0 && s.channels[head].priority <= p->priority ? head : -1;
    }
    if (group_count >= p->max_channel) {
        if ((p->flags & SOUND_CHANNEL_OLDEST) && group_oldest >= 0) return group_oldest;
        return (p->flags & SOUND_CHANNEL_PREEMPT) && group_head >= 0 && s.channels[group_head].priority < p->priority ? group_head : -1;
    }
    if (p->max_duplicates && duplicates >= p->max_duplicates)
        return p->flags & SOUND_CHANNEL_OLDEST ? duplicate : -1;
    return free_slot;
}

static bool S_StartSound(sfxcache_t *sc, float volume, LPCVECTOR2 origin, bool is_positional, int channel,
                         float attenuation, float timeofs, soundPolicy_t const *policy) {
    int selected = -1;
    unsigned priority = policy ? policy->priority : SOUND_PRIORITY(channel);
    if (!sc) return false;
    SDL_LockAudioDevice(s.device);
    if (policy && policy->cooldown_ms && s.user_cooldown[policy->user].active &&
        (int32_t)(s.user_cooldown[policy->user].end - SDL_GetTicks()) > 0) {
        SDL_UnlockAudioDevice(s.device);
        return false;
    }
    if (policy) selected = S_AdmitSound(sc, policy);
    else for (int ch = 0; ch < S_MAX_CHANNELS; ch++) {
        if (!s.channels[ch].active) { selected = ch; break; }
        if (s.channels[ch].priority < priority &&
            (selected < 0 || s.channels[ch].priority < s.channels[selected].priority)) selected = ch;
    }
    if (selected >= 0) {
        int ch = selected;
        S_EndChannel(ch);
        memset(&s.channels[ch], 0, sizeof(s.channels[ch]));
        s.channels[ch].sc           = sc;
        s.channels[ch].pos          = 0;
        s.channels[ch].master_vol   = volume;
        s.channels[ch].leftvol      = volume;
        s.channels[ch].rightvol     = volume;
        s.channels[ch].origin       = origin ? *origin : (VECTOR2){ 0.0f, 0.0f };
        s.channels[ch].attenuation  = attenuation;
        s.channels[ch].channel      = channel & 7;
        s.channels[ch].priority     = priority;
        s.channels[ch].policy = policy ? *policy : (soundPolicy_t){0};
        s.channels[ch].serial = ++s.sound_serial;
        s.channels[ch].started = SDL_GetTicks();
        s.channels[ch].delay        = (int)(timeofs * 44100.0f);
        s.channels[ch].entity       = 0;
        s.channels[ch].loop_generation = 0;
        s.channels[ch].looping      = false;
        s.channels[ch].is_positional = is_positional;
        s.channels[ch].active       = true;
        S_SoundEvent(policy, SOUND_ACCEPTED);
        SDL_UnlockAudioDevice(s.device);
        return true;
    }
    SDL_UnlockAudioDevice(s.device);
    return false;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

void S_PlaySound(uint32_t kit_id) {
    if (!s.initialized || kit_id == 0 || kit_id >= S_MAX_KITS) return;
    sSoundKit_t *k = &s.kits[kit_id];
    if (k->id != kit_id) return;
    k->registration_sequence = s.registration_sequence;
    S_StartSound(S_LoadKit(k), k->volume > 0.0f ? k->volume : 1.0f, NULL, false, 0, DEFAULT_SOUND_PACKET_ATTENUATION, 0, NULL);
}

void S_PlaySoundByName(cstring_t name) {
    if (!s.initialized || !name || !*name) return;
    sHashNode_t *n = S_FindByName(name);
    if (n) S_PlaySound(n->kit_id);
}

/* Preload a server-configstring sound so playback never blocks on archive I/O. */
void S_RegisterSound(cstring_t path) {
    if (!s.initialized || !path || !*path) return;
    sfx_t *sfx = S_FindSfx(path, true);
    if (!sfx) return;
    sfx->registration_sequence = s.registration_sequence;
    S_LoadSfx(sfx);
}

/* Play a sound by raw MPQ-relative path — non-positional (voice, UI, JASS). */
void S_PlaySoundFile(cstring_t path) {
    if (!s.initialized || !path || !*path) return;
    sfx_t *sfx = S_FindSfx(path, true);
    if (!sfx) return;
    sfx->registration_sequence = s.registration_sequence;
    S_StartSound(S_LoadSfx(sfx), 1.0f, NULL, false, 0, DEFAULT_SOUND_PACKET_ATTENUATION, 0, NULL);
}

/* Play a positional sound at a 2D world origin (distance attenuation + stereo pan). */
void S_PlaySoundAt(cstring_t path, LPCVECTOR2 origin) {
    if (!s.initialized || !path || !*path) return;
    sfx_t *sfx = S_FindSfx(path, true);
    if (!sfx) return;
    sfx->registration_sequence = s.registration_sequence;
    S_StartSound(S_LoadSfx(sfx), 1.0f, origin, true, 0, DEFAULT_SOUND_PACKET_ATTENUATION, 0, NULL);
}

void S_PlaySoundPacket(cstring_t path, LPCVECTOR3 origin, bool positioned, int channel, float volume,
                       float attenuation, float timeofs) {
    sfx_t *sfx;
    if (!s.initialized || !path || !*path) return;
    sfx = S_FindSfx(path, true);
    if (!sfx) return;
    sfx->registration_sequence = s.registration_sequence;
    S_StartSound(S_LoadSfx(sfx), volume, positioned ? &(VECTOR2){ origin->x, origin->y } : NULL, positioned,
                 channel, attenuation, timeofs, NULL);
}

void S_BeginLoopingSounds(void) {
    if (!s.initialized) return;
    if (++s.loop_generation == 0) ++s.loop_generation;
}

void S_UpdateLoopingSound(uint32_t entity, cstring_t path, LPCVECTOR2 origin, float volume, float attenuation) {
    sfx_t *sfx;
    sfxcache_t *sc;
    int free_channel = -1;

    if (!s.initialized || !entity || !path || !*path) return;
    sfx = S_FindSfx(path, true);
    if (!sfx) return;
    sfx->registration_sequence = s.registration_sequence;
    sc = S_LoadSfx(sfx);
    if (!sc) return;

    SDL_LockAudioDevice(s.device);
    FOR_LOOP(ch, S_MAX_CHANNELS) {
        if (s.channels[ch].active && s.channels[ch].looping && s.channels[ch].entity == entity) {
            if (s.channels[ch].sc != sc) {
                s.channels[ch].sc = sc;
                s.channels[ch].pos = 0;
            }
            s.channels[ch].origin = origin ? *origin : (VECTOR2){0};
            s.channels[ch].master_vol = volume;
            s.channels[ch].attenuation = attenuation;
            s.channels[ch].is_positional = origin != NULL;
            s.channels[ch].loop_generation = s.loop_generation;
            SDL_UnlockAudioDevice(s.device);
            return;
        }
        if (!s.channels[ch].active && free_channel < 0) free_channel = ch;
    }
    if (free_channel >= 0) {
        int ch = free_channel;
        memset(&s.channels[ch], 0, sizeof(s.channels[ch]));
        s.channels[ch].sc = sc;
        s.channels[ch].master_vol = volume;
        s.channels[ch].leftvol = s.channels[ch].rightvol = volume;
        s.channels[ch].origin = origin ? *origin : (VECTOR2){0};
        s.channels[ch].attenuation = attenuation;
        s.channels[ch].entity = entity;
        s.channels[ch].loop_generation = s.loop_generation;
        s.channels[ch].looping = true;
        s.channels[ch].is_positional = origin != NULL;
        s.channels[ch].active = true;
    }
    SDL_UnlockAudioDevice(s.device);
}

void S_EndLoopingSounds(void) {
    if (!s.initialized) return;
    SDL_LockAudioDevice(s.device);
    FOR_LOOP(ch, S_MAX_CHANNELS) {
        if (s.channels[ch].active && s.channels[ch].looping &&
            s.channels[ch].loop_generation != s.loop_generation)
            memset(&s.channels[ch], 0, sizeof(s.channels[ch]));
    }
    SDL_UnlockAudioDevice(s.device);
}

/* Client-owned long-form PCM streams (movie/music), stereo S16 at 44.1 kHz. */
static bool S_ValidStream(sStreamId_t stream) {
    return (uint32_t)stream < (uint32_t)S_STREAM_COUNT;
}

void S_StreamStart(sStreamId_t stream) {
    if (!s.initialized || !S_ValidStream(stream)) return;
    SDL_LockAudioDevice(s.device);
    if (!s.streams[stream].data) {
        s.streams[stream].capacity = 44100 * 2; /* two seconds keeps decoder jitter away from the callback */
        s.streams[stream].data = calloc((size_t)s.streams[stream].capacity * 2, sizeof(short));
    }
    s.streams[stream].read_pos = s.streams[stream].write_pos = s.streams[stream].count = 0;
    s.streams[stream].played_frames = 0;
    s.streams[stream].volume = 1.0f;
    s.streams[stream].paused = false;
    s.streams[stream].active = s.streams[stream].data != NULL;
    SDL_UnlockAudioDevice(s.device);
}

uint32_t S_StreamSamples(sStreamId_t stream, int16_t const *samples, uint32_t frames) {
    uint32_t written = 0;

    if (!s.initialized || !S_ValidStream(stream) || !samples || !frames ||
        !s.streams[stream].active || !s.streams[stream].data) return 0;
    SDL_LockAudioDevice(s.device);
    while (written < frames && s.streams[stream].count < s.streams[stream].capacity) {
        uint32_t dst = s.streams[stream].write_pos;
        s.streams[stream].data[dst * 2] = samples[written * 2];
        s.streams[stream].data[dst * 2 + 1] = samples[written * 2 + 1];
        s.streams[stream].write_pos = (s.streams[stream].write_pos + 1) % s.streams[stream].capacity;
        s.streams[stream].count++;
        written++;
    }
    SDL_UnlockAudioDevice(s.device);
    return written;
}

uint32_t S_StreamBufferedFrames(sStreamId_t stream) {
    uint32_t count = 0;
    if (!s.initialized || !S_ValidStream(stream) || !s.streams[stream].active) return 0;
    SDL_LockAudioDevice(s.device);
    count = s.streams[stream].count;
    SDL_UnlockAudioDevice(s.device);
    return count;
}

uint64_t S_StreamPlayedFrames(sStreamId_t stream) {
    uint64_t frames = 0;
    if (!s.initialized || !S_ValidStream(stream) || !s.streams[stream].active) return 0;
    SDL_LockAudioDevice(s.device);
    frames = s.streams[stream].played_frames;
    SDL_UnlockAudioDevice(s.device);
    return frames;
}

void S_StreamSetVolume(sStreamId_t stream, float volume) {
    if (!s.initialized || !S_ValidStream(stream)) return;
    SDL_LockAudioDevice(s.device);
    s.streams[stream].volume = MAX(0.0f, MIN(volume, 1.0f));
    SDL_UnlockAudioDevice(s.device);
}

void S_StreamSetPaused(sStreamId_t stream, bool paused) {
    if (!s.initialized || !S_ValidStream(stream)) return;
    SDL_LockAudioDevice(s.device);
    s.streams[stream].paused = paused;
    SDL_UnlockAudioDevice(s.device);
}

void S_StreamStop(sStreamId_t stream) {
    if (!s.initialized || !S_ValidStream(stream)) return;
    SDL_LockAudioDevice(s.device);
    s.streams[stream].active = false;
    s.streams[stream].paused = false;
    s.streams[stream].read_pos = s.streams[stream].write_pos = s.streams[stream].count = 0;
    s.streams[stream].played_frames = 0;
    SDL_UnlockAudioDevice(s.device);
}

void S_SetListener(LPCVECTOR2 origin, LPCVECTOR2 right) {
    s.listener.origin = *origin;
    s.listener.right  = *right;
}


bool S_PlaySoundPolicy(cstring_t path, LPCVECTOR3 origin, bool positioned, int channel, float volume,
                       float attenuation, float timeofs, soundPolicy_t const *policy) {
    sfx_t *sfx;
    if (policy && policy->request) S_ReserveSoundEvents();
    if (!s.initialized) goto rejected;
    if (!path || !*path) {
        fprintf(stderr, "S_PlaySoundPolicy: unresolved sound path (request %u)\n", policy ? policy->request : 0);
        goto rejected;
    }
    if (!policy || !policy->max_total || policy->max_total > S_MAX_CHANNELS || !policy->max_channel ||
        (policy->cooldown_ms && policy->user >= MAX_GAME_ENTITIES)) {
        fprintf(stderr, "S_PlaySoundPolicy: invalid admission limits for %s\n", path);
        goto rejected;
    }
    if (!(sfx = S_FindSfx(path, true))) goto rejected;
    sfx->registration_sequence = s.registration_sequence;
    if (S_StartSound(S_LoadSfx(sfx), volume, positioned ? &(VECTOR2){ origin->x, origin->y } : NULL,
                     positioned, channel, attenuation, timeofs, policy)) return true;
rejected:
    SDL_LockAudioDevice(s.device);
    S_SoundEvent(policy, SOUND_REJECTED);
    SDL_UnlockAudioDevice(s.device);
    return false;
}
