#include "sound/s_local.h"
#include "shared/test.h"

#include <stdio.h>

/* Generated 0.5-s stereo sine fixture; no retail audio is used. */
static DWORD sound_test_reads;

HANDLE FS_ReadFile(LPCSTR filename, LPDWORD size) {
    FILE *file;
    long length;
    BYTE *data;

    sound_test_reads++;
    if (!strcmp(filename, "stereo.wav")) {
        static BYTE const wav[] = {
            'R','I','F','F',40,0,0,0,'W','A','V','E',
            'f','m','t',' ',16,0,0,0,1,0,2,0,0x44,0xac,0,0,0x10,0xb1,2,0,2,0,8,0,
            'd','a','t','a',4,0,0,0,255,128,0,128
        };
        data = malloc(sizeof(wav));
        if (!data) return NULL;
        memcpy(data, wav, sizeof(wav));
        *size = sizeof(wav);
        return data;
    }
    if (!strcmp(filename, "broken.mp3")) {
        static BYTE const invalid[] = { 'n', 'o', 't', ' ', 'm', 'p', '3' };
        data = malloc(sizeof(invalid));
        if (!data) return NULL;
        memcpy(data, invalid, sizeof(invalid));
        *size = sizeof(invalid);
        return data;
    }
    if (strcmp(filename, "dialogue.mp3")) return NULL;
    file = fopen("tests/resources/sound-test.mp3", "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    length = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (length <= 0 || !(data = malloc((size_t)length))) { fclose(file); return NULL; }
    if (fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (DWORD)length;
    return data;
}

void FS_FreeFile(void *data) { free(data); }

static void sound_test_reset(void) {
    for (int i = 0; i < s.num_sfx; i++) free(s.known_sfx[i].cache);
    memset(&s, 0, sizeof(s));
    sound_test_reads = 0;
    s.initialized = TRUE;
}

TEST(sound, mp3_dialogue_loads_into_mono_cache) {
    sfxcache_t *cache;
    BOOL has_signal = FALSE;

    sound_test_reset();
    S_RegisterSound("dialogue.mp3");
    T_EQ(sound_test_reads, 1);
    T_EQ(s.num_sfx, 1);
    cache = s.known_sfx[0].cache;
    T_NOT_NULL(cache);
    if (!cache) return;
    T_ASSERT(cache->length > 16000);
    T_EQ(cache->loopstart, -1);
    for (int i = 0; i < cache->length; i++)
        if (cache->data[i]) { has_signal = TRUE; break; }
    T_ASSERT(has_signal);
    free(cache);
    s.known_sfx[0].cache = NULL;
}

TEST(sound, failed_load_is_cached_until_next_registration) {
    sound_test_reset();
    S_RegisterSound("broken.mp3");
    T_NULL(s.known_sfx[0].cache);
    T_EQ(sound_test_reads, 1);
    S_RegisterSound("broken.mp3");
    T_EQ(sound_test_reads, 1);
    S_BeginRegistration();
    S_RegisterSound("broken.mp3");
    T_EQ(sound_test_reads, 2);
    sound_test_reset();
}

TEST(sound, stereo_wav_downmixes_to_mono_cache) {
    sfxcache_t *cache;

    sound_test_reset();
    S_RegisterSound("stereo.wav");
    cache = s.known_sfx[0].cache;
    T_NOT_NULL(cache);
    if (!cache) return;
    T_EQ(cache->length, 2);
    T_EQ(cache->data[0], 16256);
    T_EQ(cache->data[1], -16384);
    sound_test_reset();
}
