#include "sound/s_local.h"
#include "shared/test.h"

#include <stdio.h>

/* Generated 0.5-s stereo sine fixture; no retail audio is used. */
static uint32_t sound_test_reads, sound_test_ticks;
Uint32 SDL_GetTicks(void) { return sound_test_ticks; }

handle_t FS_ReadFile(cstring_t filename, uint32_t * size) {
    FILE *file;
    long length;
    uint8_t *data;

    sound_test_reads++;
    if (!strcmp(filename, "stereo.wav")) {
        static uint8_t const wav[] = {
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
        static uint8_t const invalid[] = { 'n', 'o', 't', ' ', 'm', 'p', '3' };
        data = malloc(sizeof(invalid));
        if (!data) return NULL;
        memcpy(data, invalid, sizeof(invalid));
        *size = sizeof(invalid);
        return data;
    }
    if (strcmp(filename, "dialogue.mp3") && strncmp(filename, "voice", 5)) return NULL;
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
    *size = (uint32_t)length;
    return data;
}

void FS_FreeFile(void *data) { free(data); }

static void sound_test_reset(void) {
    S_ClearSoundEvents();
    for (int i = 0; i < s.num_sfx; i++) free(s.known_sfx[i].cache);
    memset(&s, 0, sizeof(s));
    sound_test_reads = 0;
    s.initialized = true;
}

TEST(sound, mp3_dialogue_loads_into_mono_cache) {
    sfxcache_t *cache;
    bool has_signal = false;

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
        if (cache->data[i]) { has_signal = true; break; }
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

TEST(sound, important_sound_displaces_combat_when_channels_full) {
    sound_test_reset();
    for (int i = 0; i < S_MAX_CHANNELS; i++)
        S_PlaySoundPacket("dialogue.mp3", NULL, false, 0, 0.2f, 1, 0);
    S_PlaySoundPacket("dialogue.mp3", NULL, false, CHAN_PRIORITY(1000), 0.8f, 1, 0);
    T_FEQ(s.channels[0].master_vol, 0.8f, 0.001f);
    S_PlaySoundPacket("dialogue.mp3", NULL, false, 0, 0.1f, 1, 0);
    T_FEQ(s.channels[0].master_vol, 0.8f, 0.001f);
    sound_test_reset();
}

TEST(sound, equal_priority_finishes_and_free_channels_win_before_eviction) {
    sound_test_reset();
    for (int i = 0; i < S_MAX_CHANNELS; i++)
        S_PlaySoundPacket("dialogue.mp3", NULL, false, CHAN_PRIORITY(731), 0.3f, 1, 0);
    S_PlaySoundPacket("dialogue.mp3", NULL, false, CHAN_PRIORITY(731), 0.9f, 1, 0);
    for (int i = 0; i < S_MAX_CHANNELS; i++) T_FEQ(s.channels[i].master_vol, 0.3f, 0.001f);
    s.channels[3].active = false;
    S_PlaySoundPacket("dialogue.mp3", NULL, false, CHAN_PRIORITY(900), 0.9f, 1, 0);
    T_FEQ(s.channels[0].master_vol, 0.3f, 0.001f);
    T_FEQ(s.channels[3].master_vol, 0.9f, 0.001f);
    s.channels[5].priority = 5;
    s.channels[5].looping = true;
    S_PlaySoundPacket("dialogue.mp3", NULL, false, CHAN_PRIORITY(6), 0.6f, 1, 0);
    T_FEQ(s.channels[5].master_vol, 0.6f, 0.001f);
    T_ASSERT(!s.channels[5].looping);
    sound_test_reset();
}

static bool play_policy(soundPolicy_t policy, float volume) {
    return S_PlaySoundPolicy("dialogue.mp3", NULL, false, 0, volume, 1, 0, &policy);
}

TEST(sound, authored_channel_allows_three_units_and_rejects_fourth_equal_voice) {
    soundPolicy_t p = { .priority = 1000, .group = 1, .max_channel = 3, .max_total = 24,
        .max_duplicates = 4, .flags = SOUND_NO_DUPLICATE_USERS | SOUND_CHANNEL_PREEMPT };
    sound_test_reset();
    for (int i = 1; i <= 3; i++) { p.user = i; T_ASSERT(play_policy(p, .3f)); }
    p.user = 4; T_ASSERT(!play_policy(p, .9f));
    p.user = 1; p.priority = 2000; T_ASSERT(!play_policy(p, .9f));
    p.user = 4; T_ASSERT(play_policy(p, .8f));
    T_FEQ(s.channels[2].master_vol, .8f, .001f);
    sound_test_reset();
}

TEST(sound, priority_requires_permission_and_oldest_allows_equal) {
    soundPolicy_t p = { .priority = 100, .group = 7, .max_channel = 1, .max_total = 24, .max_duplicates = 4 };
    sound_test_reset();
    T_ASSERT(play_policy(p, .2f));
    p.priority = 200; T_ASSERT(!play_policy(p, .8f));
    p.priority = 100; p.flags = SOUND_CHANNEL_PREEMPT; T_ASSERT(!play_policy(p, .8f));
    p.flags = SOUND_CHANNEL_OLDEST; T_ASSERT(play_policy(p, .8f));
    T_FEQ(s.channels[0].master_vol, .8f, .001f);
    sound_test_reset();
}

TEST(sound, duplicate_file_preemption_is_strict_and_user_preemption_is_unconditional) {
    soundPolicy_t p = { .priority = 100, .user = 23, .group = 1, .max_channel = 3, .max_total = 24,
        .max_duplicates = 4, .flags = SOUND_NO_DUPLICATES | SOUND_DUPLICATE_PREEMPT };
    sound_test_reset();
    T_ASSERT(play_policy(p, .2f));
    T_ASSERT(!play_policy(p, .8f));
    p.priority++; T_ASSERT(play_policy(p, .8f));
    p.priority = 1; p.flags = SOUND_NO_DUPLICATE_USERS | SOUND_USER_PREEMPT;
    T_ASSERT(play_policy(p, .4f));
    T_FEQ(s.channels[0].master_vol, .4f, .001f);
    sound_test_reset();
}

TEST(sound, response_cooldown_starts_at_actual_completion_and_preemption) {
    soundPolicy_t p = { .priority = 1000, .user = 4, .group = 1, .max_channel = 3, .max_total = 24,
        .max_duplicates = 4, .cooldown_ms = 250, .flags = SOUND_NO_DUPLICATE_USERS };
    int16_t out[4];
    sound_test_reset();
    sound_test_ticks = 1000;
    T_ASSERT(play_policy(p, .2f));
    T_ASSERT(!play_policy(p, .2f));
    s.channels[0].pos = s.channels[0].sc->length;
    S_TestMix(out, 2); /* actual device completion, not a game-time deadline */
    sound_test_ticks = 1249; T_ASSERT(!play_policy(p, .2f));
    sound_test_ticks = 1250; T_ASSERT(play_policy(p, .2f));
    soundPolicy_t q = p; q.user = 5; q.max_channel = 1; q.flags = SOUND_CHANNEL_OLDEST;
    T_ASSERT(play_policy(q, .3f));
    sound_test_ticks = 1499; T_ASSERT(!play_policy(p, .2f));
    sound_test_ticks = 1500; T_ASSERT(play_policy(p, .2f));
    sound_test_reset();
}

TEST(sound, global_limit_requires_flags_and_accepts_equal_priority) {
    soundPolicy_t p = { .priority = 1000, .group = 1, .max_channel = 24, .max_total = 24, .max_duplicates = 4 };
    sound_test_reset();
    for (int i = 0; i < 24; i++) {
        char path[32]; snprintf(path, sizeof(path), "voice%d.mp3", i);
        T_ASSERT(S_PlaySoundPolicy(path, NULL, false, 0, .2f, 1, 0, &p));
    }
    p.priority = 2000; T_ASSERT(!play_policy(p, .8f));
    p.priority = 999; p.flags = SOUND_LIST_PREEMPT; T_ASSERT(!play_policy(p, .8f));
    p.priority = 1000; T_ASSERT(play_policy(p, .8f));
    T_FEQ(s.channels[23].master_vol, .8f, .001f); /* newest equal-priority channel head */
    p.priority = 1; p.flags = SOUND_LIST_OLDEST; T_ASSERT(play_policy(p, .4f));
    T_FEQ(s.channels[0].master_vol, .4f, .001f);
    sound_test_reset();
}

TEST(sound, filename_cap_is_separate_from_channel_cap) {
    soundPolicy_t p = { .priority = 100, .group = 0, .max_channel = 16, .max_total = 24, .max_duplicates = 4 };
    sound_test_reset();
    for (int i = 0; i < 4; i++) T_ASSERT(play_policy(p, .2f));
    T_ASSERT(!play_policy(p, .8f));
    p.flags = SOUND_CHANNEL_PREEMPT; p.priority = 200; T_ASSERT(!play_policy(p, .8f));
    p.flags = SOUND_CHANNEL_OLDEST; p.priority = 1; T_ASSERT(play_policy(p, .8f));
    T_FEQ(s.channels[0].master_vol, .8f, .001f);
    sound_test_reset();
}

TEST(sound, cooldown_wraps_and_map_stop_clears_identity) {
    soundPolicy_t p = { .priority = 1000, .user = 4, .group = 1, .max_channel = 3, .max_total = 24,
        .max_duplicates = 4, .cooldown_ms = 250, .flags = SOUND_NO_DUPLICATE_USERS };
    int16_t out[4];
    sound_test_reset();
    sound_test_ticks = 0xffffff80u;
    T_ASSERT(play_policy(p, .2f));
    s.channels[0].pos = s.channels[0].sc->length;
    S_TestMix(out, 2);
    sound_test_ticks = 121; T_ASSERT(!play_policy(p, .2f));
    sound_test_ticks = 122; T_ASSERT(play_policy(p, .2f));
    S_StopAllSounds(); T_ASSERT(play_policy(p, .2f));
    sound_test_reset();
}

TEST(sound, oldest_channel_ties_follow_retail_priority_list_order) {
    soundPolicy_t p = { .priority = 100, .group = 1, .max_channel = 2, .max_total = 24, .max_duplicates = 4 };
    sound_test_reset(); sound_test_ticks = 100;
    T_ASSERT(play_policy(p, .2f)); T_ASSERT(play_policy(p, .3f));
    p.flags = SOUND_CHANNEL_OLDEST;
    T_ASSERT(play_policy(p, .8f));
    T_FEQ(s.channels[1].master_vol, .8f, .001f); /* same timestamp: first in priority list */
    sound_test_reset();
}

/* 6f0b0c50 excludes IGNOREUSERNAME instances from the user hash. The flag
 * excludes existing sounds; it does not bypass an incoming NODUPEUSERNAMES check. */
TEST(sound, ignored_user_is_neither_duplicate_nor_user_preemption_victim) {
    soundPolicy_t p = { .priority = 100, .user = 12, .group = 1, .max_channel = 3, .max_total = 24,
        .max_duplicates = 4, .flags = SOUND_IGNORE_USER };
    sound_test_reset();
    T_ASSERT(play_policy(p, .2f));
    p.flags = SOUND_NO_DUPLICATE_USERS;
    T_ASSERT(play_policy(p, .3f));
    p.flags |= SOUND_IGNORE_USER;
    T_ASSERT(!play_policy(p, .4f));
    p.flags = SOUND_NO_DUPLICATE_USERS | SOUND_USER_PREEMPT;
    T_ASSERT(play_policy(p, .8f));
    T_FEQ(s.channels[0].master_vol, .2f, .001f);
    T_FEQ(s.channels[1].master_vol, .8f, .001f);
    sound_test_reset();
}

TEST(sound, final_sample_releases_channel_without_an_extra_device_callback) {
    soundPolicy_t p = { .priority = 1000, .user = 4, .group = 1, .max_channel = 1, .max_total = 24,
        .max_duplicates = 4, .cooldown_ms = 250, .flags = SOUND_NO_DUPLICATE_USERS };
    int16_t out[4];
    sound_test_reset(); sound_test_ticks = 1000;
    T_ASSERT(play_policy(p, .2f));
    s.channels[0].pos = s.channels[0].sc->length - 2;
    S_TestMix(out, 2);
    T_ASSERT(!s.channels[0].active);
    sound_test_ticks = 1249; T_ASSERT(!play_policy(p, .2f));
    sound_test_ticks = 1250; T_ASSERT(play_policy(p, .2f));
    sound_test_reset();
}

static void sound_expect_event(uint32_t request, uint32_t kind) {
    soundEvent_t event;
    T_ASSERT(S_PollSoundEvent(&event));
    T_EQ(event.request, request); T_EQ(event.event, kind); T_EQ(event.user, 4);
}

TEST(sound, feedback_follows_admission_delay_rejection_and_preemption) {
    soundPolicy_t p = { .priority = 1000, .user = 4, .request = 1, .group = 1, .max_channel = 3,
        .max_total = 24, .max_duplicates = 4, .flags = SOUND_NO_DUPLICATE_USERS };
    int16_t out[882]; soundEvent_t event;
    sound_test_reset();
    T_ASSERT(S_PlaySoundPolicy("dialogue.mp3", NULL, false, 0, 1, 0, .01f, &p));
    sound_expect_event(1, SOUND_ACCEPTED);
    T_ASSERT(!S_PollSoundEvent(&event));
    S_TestMix(out, 441); /* delay only: no sample and no portrait start */
    T_ASSERT(!S_PollSoundEvent(&event));
    S_TestMix(out, 1); sound_expect_event(1, SOUND_STARTED);
    p.request = 2; T_ASSERT(!play_policy(p, 1)); sound_expect_event(2, SOUND_REJECTED);
    p.request = 3; p.flags |= SOUND_USER_PREEMPT;
    T_ASSERT(play_policy(p, 1));
    sound_expect_event(1, SOUND_ENDED); sound_expect_event(3, SOUND_ACCEPTED);
    S_TestMix(out, 1); sound_expect_event(3, SOUND_STARTED);
    s.channels[0].pos = s.channels[0].sc->length - 1;
    S_TestMix(out, 1); sound_expect_event(3, SOUND_ENDED);
    S_TestMix(out, 1); T_ASSERT(!S_PollSoundEvent(&event));
    sound_test_reset();
}

TEST(sound, feedback_rejects_missing_audio_and_survives_backpressure_and_stop) {
    soundPolicy_t p = { .user = 4, .request = 1, .max_channel = 3, .max_total = 24,
        .max_duplicates = 4, .flags = SOUND_NO_DUPLICATE_USERS };
    soundEvent_t event;
    sound_test_reset();
    T_ASSERT(!S_PlaySoundPolicy("missing.wav", NULL, false, 0, 1, 0, 0, &p));
    sound_expect_event(1, SOUND_REJECTED);
    p.request = 2; s.initialized = false;
    T_ASSERT(!play_policy(p, 1)); sound_expect_event(2, SOUND_REJECTED);
    s.initialized = true; p.request = 3; T_ASSERT(play_policy(p, 1));
    for (int i = 4; i < 304; i++) { p.request = i; T_ASSERT(!play_policy(p, 1)); }
    S_StopAllSounds();
    sound_expect_event(3, SOUND_ACCEPTED);
    for (int i = 4; i < 304; i++) sound_expect_event(i, SOUND_REJECTED);
    sound_expect_event(3, SOUND_ENDED); /* stopped before its first sample */
    T_ASSERT(!S_PollSoundEvent(&event));
    sound_test_reset();
}
