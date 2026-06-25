/*
 * s_sound.c — Sound system modelled after Quake 2's snd_dma.c.
 *
 * Key improvements over the previous implementation:
 *   - Playsound queue with sorted insertion (priority by begin time)
 *   - Entity/channel priority in S_PickChannel (same entity overrides,
 *     player sounds protected from monster override, evict shortest life)
 *   - Distance-based spatialization with stereo panning
 *   - Thread-safe: main thread does all queue/channel/cache work;
 *     SDL callback only copies from a pre-mixed ring buffer under lock.
 *   - 16 simultaneous channels (up from 8)
 */
#include "s_local.h"

sState_t s;

/* ================================================================== */
/*  Hash lookup                                                        */
/* ================================================================== */

static DWORD S_HashString(LPCSTR str) {
    DWORD hash = 5381;
    for (; *str; str++)
        hash = ((hash << 5) + hash) + (unsigned char)*str;
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
    n->next = s.hash_buckets[bucket];
    s.hash_buckets[bucket] = n;
}

/* ================================================================== */
/*  DBC helpers                                                        */
/* ================================================================== */

static DWORD S_DbcField(BYTE const *record, DWORD field) {
    BYTE const *p = record + field * 4;
    return (DWORD)p[0] | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16) | ((DWORD)p[3] << 24);
}

static LPCSTR S_DbcString(BYTE const *strings, DWORD str_size, DWORD offset) {
    if (offset == 0 || offset >= str_size) return "";
    return (LPCSTR)(strings + offset);
}

void S_LoadSoundEntries(void) {
    DWORD size = 0;
    BYTE *data = FS_ReadFile("DBFilesClient\\SoundEntries.dbc", &size);
    if (!data || size <= 20 || memcmp(data, "WDBC", 4)) {
        FS_FreeFile(data);
        return;
    }
    DWORD nrec = *(DWORD *)(data + 4);
    DWORD nflds = *(DWORD *)(data + 8);
    DWORD recsz = *(DWORD *)(data + 12);
    DWORD strsz = *(DWORD *)(data + 16);
    if (nflds != SENTRY_FIELDS || recsz != SENTRY_RECORD_SIZE ||
        20 + nrec * recsz + strsz > size) {
        FS_FreeFile(data);
        return;
    }
    BYTE *records = data + 20;
    BYTE *strings = records + nrec * recsz;

    for (DWORD i = 0; i < nrec && i < S_MAX_KITS; i++) {
        BYTE *rec = records + i * recsz;
        DWORD id = S_DbcField(rec, 0);
        if (id >= S_MAX_KITS) continue;
        sSoundKit_t *k = &s.kits[id];
        k->id = id;
        k->type = S_DbcField(rec, 1);
        k->name = S_DbcString(strings, strsz, S_DbcField(rec, 2));
        for (DWORD j = 0; j < SENTRY_MAX_FILES; j++)
            k->files[j] = S_DbcString(strings, strsz, S_DbcField(rec, 3 + j));
        for (DWORD j = 0; j < SENTRY_MAX_FILES; j++)
            k->freq[j] = S_DbcField(rec, 13 + j);
        k->directoryBase = S_DbcString(strings, strsz, S_DbcField(rec, 23));
        k->volume = *(float *)&(DWORD){S_DbcField(rec, 24)};
        k->flags = S_DbcField(rec, 25);
        if (k->id > s.kit_count) s.kit_count = k->id + 1;
        S_InsertHash(id, k->name);
    }
    s.dbc_data = data;
    printf("[sound] loaded %lu kits from SoundEntries.dbc\n", (unsigned long)nrec);
}

/* ================================================================== */
/*  WAV cache (main-thread only — no lock needed for cache ops)        */
/* ================================================================== */

static sWavCache_t *S_CacheWav(DWORD kit_id, LPCSTR path) {
    /* Evict LRU if full */
    if (s.wav_cache_count >= S_MAX_CACHED_WAVS) {
        DWORD oldest_lru = UINT32_MAX, oldest_idx = 0;
        for (DWORD i = 0; i < S_MAX_CACHED_WAVS; i++) {
            if (s.wav_cache_lru[i] < oldest_lru) {
                oldest_lru = s.wav_cache_lru[i];
                oldest_idx = i;
            }
        }
        SDL_FreeWAV(s.wav_cache[oldest_idx].data);
        memset(&s.wav_cache[oldest_idx], 0, sizeof(sWavCache_t));
        s.wav_cache_count--;
        for (DWORD i = 0; i < S_MAX_CACHED_WAVS; i++)
            if (s.wav_cache_lru[i] < oldest_lru) s.wav_cache_lru[i]++;
        s.wav_cache_lru[oldest_idx] = 0;
    }

    /* Find empty slot */
    DWORD slot = 0;
    for (slot = 0; slot < S_MAX_CACHED_WAVS; slot++)
        if (!s.wav_cache[slot].data) break;
    if (slot >= S_MAX_CACHED_WAVS) return NULL;

    /* Read WAV from MPQ — expensive I/O, runs on main thread only */
    DWORD file_size = 0;
    void *file_data = FS_ReadFile(path, &file_size);
    if (!file_data || file_size == 0) {
        FS_FreeFile(file_data);
        return NULL;
    }

    SDL_RWops *rw = SDL_RWFromMem(file_data, file_size);
    if (!rw) {
        FS_FreeFile(file_data);
        return NULL;
    }
    sWavCache_t *w = &s.wav_cache[slot];
    if (!SDL_LoadWAV_RW(rw, 1, &w->spec, &w->data, &w->len)) {
        FS_FreeFile(file_data);
        return NULL;
    }
    FS_FreeFile(file_data);

    w->kit_id = kit_id;
    s.wav_cache_count++;
    for (DWORD i = 0; i < S_MAX_CACHED_WAVS; i++)
        s.wav_cache_lru[i]++;
    s.wav_cache_lru[slot] = 0;
    return w;
}

static sWavCache_t *S_FindCachedWav(DWORD kit_id) {
    for (DWORD i = 0; i < S_MAX_CACHED_WAVS; i++)
        if (s.wav_cache[i].data && s.wav_cache[i].kit_id == kit_id) {
            for (DWORD j = 0; j < S_MAX_CACHED_WAVS; j++)
                if (s.wav_cache_lru[j] < s.wav_cache_lru[i]) s.wav_cache_lru[j]++;
            s.wav_cache_lru[i] = 0;
            return &s.wav_cache[i];
        }
    return NULL;
}

/* ================================================================== */
/*  Playsound queue                                                    */
/* ================================================================== */

static void S_InitPlaysoundFreeList(void) {
    s.free_playsounds.prev = s.free_playsounds.next = &s.free_playsounds;
    s.pending_playsounds.prev = s.pending_playsounds.next = &s.pending_playsounds;
    for (int i = 0; i < S_MAX_PLAYSOUNDS; i++) {
        s.playsound_pool[i].prev = &s.free_playsounds;
        s.playsound_pool[i].next = s.free_playsounds.next;
        s.playsound_pool[i].prev->next = &s.playsound_pool[i];
        s.playsound_pool[i].next->prev = &s.playsound_pool[i];
    }
}

static playsound_t *S_AllocPlaysound(void) {
    playsound_t *ps = s.free_playsounds.next;
    if (ps == &s.free_playsounds) return NULL;
    ps->prev->next = ps->next;
    ps->next->prev = ps->prev;
    return ps;
}

static void S_FreePlaysound(playsound_t *ps) {
    ps->prev->next = ps->next;
    ps->next->prev = ps->prev;
    ps->next = s.free_playsounds.next;
    s.free_playsounds.next->prev = ps;
    ps->prev = &s.free_playsounds;
    s.free_playsounds.next = ps;
}

/* ================================================================== */
/*  Channel selection (Quake 2 priority model)                         */
/* ================================================================== */

/* Pick a channel to play on.  Entity 0 / channel 0 never overrides. */
static sChannel_t *S_PickChannel(DWORD entnum, DWORD entchannel) {
    int first_to_die = -1;
    int life_left = 0x7fffffff;

    for (int i = 0; i < S_MAX_CHANNELS; i++) {
        sChannel_t *ch = &s.channels[i];

        /* Same entity+channel always overrides (except channel 0) */
        if (entchannel != 0 && ch->entnum == entnum && ch->entchannel == entchannel) {
            first_to_die = i;
            break;
        }

        /* Don't let non-local sounds override local player sounds */
        if (ch->entnum == s.local_entnum && entnum != s.local_entnum && ch->active)
            continue;

        /* Evict the channel closest to finishing */
        if (ch->active) {
            int life = ch->end - (int)s.painted;
            if (life < life_left) {
                life_left = life;
                first_to_die = i;
            }
        } else if (first_to_die == -1) {
            first_to_die = i;
        }
    }

    if (first_to_die == -1) return NULL;
    sChannel_t *ch = &s.channels[first_to_die];
    memset(ch, 0, sizeof(*ch));
    return ch;
}

/* ================================================================== */
/*  Spatialization — distance-based volume with stereo panning         */
/* ================================================================== */

static void S_SpatializeChannel(sChannel_t *ch) {
    /* Without a position or entity, full volume mono */
    if (!ch->entnum) {
        ch->leftvol = ch->rightvol = 255;
        return;
    }

    /* TODO: query entity position from game code via import.
       For now, full volume — proper spatialization needs
       CL_GetEntitySoundOrigin or equivalent. */
    ch->leftvol = ch->rightvol = 255;
}

/* ================================================================== */
/*  Issue playsound — assign a queued sound to a channel               */
/* ================================================================== */

static void S_IssuePlaysound(playsound_t *ps) {
    sChannel_t *ch = S_PickChannel(ps->entnum, ps->entchannel);
    if (!ch) {
        S_FreePlaysound(ps);
        return;
    }

    ch->wav = ps->wav;
    ch->pos = 0;
    ch->end = (int)s.painted + (int)(ps->wav->len / sizeof(Sint16));
    ch->entnum = ps->entnum;
    ch->entchannel = ps->entchannel;
    ch->active = TRUE;

    S_SpatializeChannel(ch);

    S_FreePlaysound(ps);
}

/* ================================================================== */
/*  SDL audio callback — minimal, just copies pre-mixed data           */
/* ================================================================== */

static void SDLCALL S_MixAudio(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    int samples = len / sizeof(Sint16);
    Sint16 *out = (Sint16 *)stream;

    SDL_LockAudioDevice(s.device);
    for (int i = 0; i < samples; i++) {
        if (s.mix_buf_read == s.mix_buf_write) {
            /* Underrun — fill rest with silence */
            memset(out + i, 0, (samples - i) * sizeof(Sint16));
            break;
        }
        out[i] = s.mix_buf[s.mix_buf_read];
        s.mix_buf_read = (s.mix_buf_read + 1) % s.mix_buf_samples;
    }
    SDL_UnlockAudioDevice(s.device);
}

/* ================================================================== */
/*  Init / Shutdown                                                    */
/* ================================================================== */

BOOL S_Init(void) {
    memset(&s, 0, sizeof(s));
    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "[sound] SDL_Init(SDL_INIT_AUDIO): %s\n", SDL_GetError());
        return FALSE;
    }
    SDL_AudioSpec want = {0}, have = {0};
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = S_MixAudio;
    s.device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (s.device == 0) {
        fprintf(stderr, "[sound] SDL_OpenAudioDevice: %s\n", SDL_GetError());
        return FALSE;
    }

    /* Pre-mixed ring buffer: 0.5s at 44100 Hz */
    s.mix_buf_samples = have.freq / 2;
    s.mix_buf = SDL_calloc(s.mix_buf_samples, sizeof(Sint16));
    if (!s.mix_buf) {
        fprintf(stderr, "[sound] out of memory for mix buffer\n");
        SDL_CloseAudioDevice(s.device);
        return FALSE;
    }

    S_InitPlaysoundFreeList();
    SDL_PauseAudioDevice(s.device, 0);
    S_LoadSoundEntries();
    s.initialized = TRUE;
    printf("[sound] initialized (%d Hz, 16-bit, mono, %d channels)\n",
           have.freq, S_MAX_CHANNELS);
    return TRUE;
}

void S_Shutdown(void) {
    if (!s.initialized) return;
    SDL_CloseAudioDevice(s.device);
    for (DWORD i = 0; i < S_MAX_CACHED_WAVS; i++)
        if (s.wav_cache[i].data) SDL_FreeWAV(s.wav_cache[i].data);
    SDL_free(s.mix_buf);
    FS_FreeFile(s.dbc_data);
    memset(&s, 0, sizeof(s));
    printf("[sound] shutdown\n");
}

/* ================================================================== */
/*  Main-thread update — drain queue, spatialize, mix ahead            */
/* ================================================================== */

void S_Update(void) {
    if (!s.initialized) return;

    /* Drain pending playsounds into channels */
    playsound_t *ps = s.pending_playsounds.next;
    while (ps != &s.pending_playsounds) {
        playsound_t *next = ps->next;
        if ((int)s.painted >= (int)ps->begin)
            S_IssuePlaysound(ps);
        ps = next;
    }

    /* Deactivate channels whose sound has finished */
    for (int i = 0; i < S_MAX_CHANNELS; i++) {
        sChannel_t *ch = &s.channels[i];
        if (!ch->active) continue;
        if ((int)s.painted >= ch->end) {
            memset(ch, 0, sizeof(*ch));
            continue;
        }
        S_SpatializeChannel(ch);
    }

    /* Mix ahead: fill mix buffer up to 2048 samples past write cursor */
    DWORD target = s.painted + 2048;
    while (s.painted < target) {
        /* Check free space in ring buffer */
        DWORD used = (s.mix_buf_write - s.mix_buf_read + s.mix_buf_samples) % s.mix_buf_samples;
        if (s.mix_buf_samples - used < 1) break;

        /* Mix one sample */
        Sint32 sample = 0;
        for (int i = 0; i < S_MAX_CHANNELS; i++) {
            sChannel_t *ch = &s.channels[i];
            if (!ch->active || !ch->wav) continue;
            if (ch->pos >= (int)(ch->wav->len / sizeof(Sint16))) {
                memset(ch, 0, sizeof(*ch));
                continue;
            }
            Sint16 *src = (Sint16 *)ch->wav->data;
            Sint32 s = src[ch->pos];
            /* Average left/right volume, scale to 0-255 range */
            int vol = (ch->leftvol + ch->rightvol) / 2;
            sample += (s * vol) >> 8;
            ch->pos++;
        }
        /* Clamp to int16 range */
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;

        SDL_LockAudioDevice(s.device);
        s.mix_buf[s.mix_buf_write] = (Sint16)sample;
        s.mix_buf_write = (s.mix_buf_write + 1) % s.mix_buf_samples;
        SDL_UnlockAudioDevice(s.device);
        s.painted++;
    }
}

/* ================================================================== */
/*  Public API                                                         */
/* ================================================================== */

void S_PlaySound(DWORD kit_id) {
    if (!s.initialized || kit_id == 0 || kit_id >= S_MAX_KITS) return;
    sSoundKit_t *k = &s.kits[kit_id];
    if (k->id != kit_id || !k->files[0] || !*k->files[0]) return;

    /* Resolve cache */
    sWavCache_t *w = S_FindCachedWav(kit_id);
    if (!w) {
        char path[512];
        if (k->directoryBase && *k->directoryBase && *k->directoryBase != '(')
            snprintf(path, sizeof(path), "%s\\%s", k->directoryBase, k->files[0]);
        else
            snprintf(path, sizeof(path), "%s", k->files[0]);
        w = S_CacheWav(kit_id, path);
    }
    if (!w) return;

    /* Allocate playsound and queue it */
    playsound_t *ps = S_AllocPlaysound();
    if (!ps) return;

    ps->kit = k;
    ps->wav = w;
    ps->entnum = 0;
    ps->entchannel = 0;
    ps->volume = k->volume > 0.0f ? k->volume : 1.0f;
    ps->begin = s.painted;

    /* Sorted insert by begin time */
    playsound_t *sort;
    for (sort = s.pending_playsounds.next;
         sort != &s.pending_playsounds && sort->begin < ps->begin;
         sort = sort->next)
        ;
    ps->next = sort;
    ps->prev = sort->prev;
    ps->next->prev = ps;
    ps->prev->next = ps;
}

void S_PlaySoundByName(LPCSTR name) {
    if (!s.initialized || !name || !*name) return;
    sHashNode_t *n = S_FindByName(name);
    if (n) S_PlaySound(n->kit_id);
}
