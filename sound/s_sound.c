/*
 * s_sound.c — Lightweight UI sound system using SDL2 low-level audio.
 *
 * Loads SoundEntries.dbc to map kit IDs/names to file paths,
 * decodes WAV files via SDL_LoadWAV_RW from MPQ archives,
 * and mixes up to S_MAX_CHANNELS simultaneous sounds in an
 * SDL audio callback.
 */
#include "s_local.h"

#define STB_VORBIS_IMPLEMENTATION
#include "stb_vorbis.c"

sState_t s;

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

/* Read a 32-bit little-endian field from a DBC record. */
static DWORD S_DbcField(BYTE const *record, DWORD field) {
    BYTE const *p = record + field * 4;
    return (DWORD)p[0] | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16) | ((DWORD)p[3] << 24);
}

/* Resolve a DBC string offset to a pointer within the string block. */
static LPCSTR S_DbcString(BYTE const *strings, DWORD str_size, DWORD offset) {
    if (offset == 0 || offset >= str_size) return "";
    return (LPCSTR)(strings + offset);
}

#define DBC_MAGIC 0x43424457u

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
        free(s.wav_cache[oldest_idx].data);
        memset(&s.wav_cache[oldest_idx], 0, sizeof(sWavCache_t));
        s.wav_cache_count--;
        /* Shift LRU counters */
        for (DWORD i = 0; i < S_MAX_CACHED_WAVS; i++)
            if (s.wav_cache_lru[i] < oldest_lru) s.wav_cache_lru[i]++;
        s.wav_cache_lru[oldest_idx] = 0;
    }

    /* Find empty slot */
    DWORD slot = 0;
    for (slot = 0; slot < S_MAX_CACHED_WAVS; slot++)
        if (!s.wav_cache[slot].data) break;
    if (slot >= S_MAX_CACHED_WAVS) return NULL;

    /* Read WAV from MPQ */
    DWORD file_size = 0;
    void *file_data = FS_ReadFile(path, &file_size);
    if (!file_data || file_size == 0) {
        FS_FreeFile(file_data);
        return NULL;
    }

    sWavCache_t *w = &s.wav_cache[slot];
    int channels, sample_rate;
    short *pcm = NULL;
    int num_samples = stb_vorbis_decode_memory((const unsigned char *)file_data, (int)file_size, &channels, &sample_rate, &pcm);
    FS_FreeFile(file_data);
    if (num_samples <= 0 || !pcm) {
        fprintf(stderr, "[sound] stb_vorbis decode failed for %s\n", path);
        return NULL;
    }
    w->data = (Uint8 *)pcm;
    w->len = (Uint32)(num_samples * channels * sizeof(short));
    w->spec.format = AUDIO_S16SYS;
    w->spec.channels = channels;
    w->spec.freq = sample_rate;

    w->kit_id = kit_id;
    s.wav_cache_count++;
    /* Reset all LRU counters, mark this slot as most recent */
    for (DWORD i = 0; i < S_MAX_CACHED_WAVS; i++)
        s.wav_cache_lru[i]++;
    s.wav_cache_lru[slot] = 0;
    return w;
}

static sWavCache_t *S_FindCachedWav(DWORD kit_id) {
    for (DWORD i = 0; i < S_MAX_CACHED_WAVS; i++)
        if (s.wav_cache[i].data && s.wav_cache[i].kit_id == kit_id) {
            /* Touch LRU */
            for (DWORD j = 0; j < S_MAX_CACHED_WAVS; j++)
                if (s.wav_cache_lru[j] < s.wav_cache_lru[i]) s.wav_cache_lru[j]++;
            s.wav_cache_lru[i] = 0;
            return &s.wav_cache[i];
        }
    return NULL;
}

static void SDLCALL S_MixAudio(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    memset(stream, 0, len);
    Sint16 *out = (Sint16 *)stream;
    int samples = len / sizeof(Sint16);

    for (DWORD ch = 0; ch < S_MAX_CHANNELS; ch++) {
        if (!s.channels[ch].active || !s.channels[ch].wav) continue;
        sWavCache_t *w = s.channels[ch].wav;
        float vol = s.channels[ch].volume;
        int remaining = samples;
        Uint32 pos = s.channels[ch].pos;
        Sint16 *src = (Sint16 *)w->data;
        int total_samples = w->len / sizeof(Sint16);
        int src_samples = total_samples;

        for (int i = 0; i < remaining; i++) {
            if ((int)pos >= src_samples) {
                s.channels[ch].active = FALSE;
                break;
            }
            out[i] = (Sint16)(src[pos] * vol);
            pos++;
        }
        s.channels[ch].pos = pos;
    }
}

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
    SDL_PauseAudioDevice(s.device, 0);
    S_LoadSoundEntries();
    s.initialized = TRUE;
    printf("[sound] initialized (44100 Hz, 16-bit, mono)\n");
    return TRUE;
}

void S_Shutdown(void) {
    if (!s.initialized) return;
    SDL_CloseAudioDevice(s.device);
    for (DWORD i = 0; i < S_MAX_CACHED_WAVS; i++)
        if (s.wav_cache[i].data) free(s.wav_cache[i].data);
    FS_FreeFile(s.dbc_data);
    memset(&s, 0, sizeof(s));
    printf("[sound] shutdown\n");
}

void S_PlaySound(DWORD kit_id) {
    if (!s.initialized || kit_id == 0 || kit_id >= S_MAX_KITS) return;
    sSoundKit_t *k = &s.kits[kit_id];
    if (k->id != kit_id || !k->files[0] || !*k->files[0]) return;

    /* Check cache first */
    sWavCache_t *w = S_FindCachedWav(kit_id);
    if (!w) {
        /* Build full path: directoryBase + "\" + file[0] */
        char path[512];
        if (k->directoryBase && *k->directoryBase && *k->directoryBase != '(') {
            snprintf(path, sizeof(path), "%s\\%s", k->directoryBase, k->files[0]);
        } else {
            snprintf(path, sizeof(path), "%s", k->files[0]);
        }
        w = S_CacheWav(kit_id, path);
    }
    if (!w) return;

    /* Find a free channel */
    SDL_LockAudioDevice(s.device);
    for (DWORD ch = 0; ch < S_MAX_CHANNELS; ch++) {
        if (!s.channels[ch].active) {
            s.channels[ch].wav = w;
            s.channels[ch].pos = 0;
            s.channels[ch].volume = k->volume > 0.0f ? k->volume : 1.0f;
            s.channels[ch].active = TRUE;
            SDL_UnlockAudioDevice(s.device);
            printf("[sound] play id=%lu name=%s\n", (unsigned long)kit_id, k->name);
            return;
        }
    }
    SDL_UnlockAudioDevice(s.device);
}

void S_PlaySoundByName(LPCSTR name) {
    if (!s.initialized || !name || !*name) return;
    sHashNode_t *n = S_FindByName(name);
    if (n) S_PlaySound(n->kit_id);
}

/* Play a sound by its raw MPQ-relative file path (e.g. "Units\\Human\\Footman\\FootmanYesAttack1.wav").
 * Uses kit_id=0 slot in wav_cache as a path-keyed one-shot entry.
 * kit_id=0 is never used by the DBC kit system (IDs start at 1). */
void S_PlaySoundFile(LPCSTR path) {
    if (!s.initialized || !path || !*path) return;
    /* Look for already-cached entry matching this path. */
    for (DWORD i = 0; i < S_MAX_CACHED_WAVS; i++) {
        if (s.wav_cache[i].data && s.wav_cache[i].kit_id == 0 &&
            s.wav_cache[i].path && !strcasecmp(s.wav_cache[i].path, path)) {
            SDL_LockAudioDevice(s.device);
            for (DWORD ch = 0; ch < S_MAX_CHANNELS; ch++) {
                if (!s.channels[ch].active) {
                    s.channels[ch].wav = &s.wav_cache[i];
                    s.channels[ch].pos = 0;
                    s.channels[ch].volume = 1.0f;
                    s.channels[ch].active = TRUE;
                    SDL_UnlockAudioDevice(s.device);
                    return;
                }
            }
            SDL_UnlockAudioDevice(s.device);
            return;
        }
    }
    /* Not cached — load it. */
    sWavCache_t *w = S_CacheWav(0, path);
    if (!w) return;
    /* Tag it with the path so future lookups find it. */
    w->path = path;
    SDL_LockAudioDevice(s.device);
    for (DWORD ch = 0; ch < S_MAX_CHANNELS; ch++) {
        if (!s.channels[ch].active) {
            s.channels[ch].wav = w;
            s.channels[ch].pos = 0;
            s.channels[ch].volume = 1.0f;
            s.channels[ch].active = TRUE;
            SDL_UnlockAudioDevice(s.device);
            return;
        }
    }
    SDL_UnlockAudioDevice(s.device);
}
