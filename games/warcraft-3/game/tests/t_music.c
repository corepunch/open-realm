#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"

extern player_t * currentplayer;

TEST(wc3_music, set_map_defers_replacement_until_current_map_track_finishes) {
    gameClient_t * client = &game.clients[0];
    player_t * previous = currentplayer;
    wc3MusicState_t saved = client->music;
    uint32_t old_session, new_session;

    currentplayer = &client->ps;
    memset(&client->music, 0, sizeof(client->music));
    G_MusicSetMap("OldMapMusic", false, 2);
    old_session = client->music.current_session_id;
    T_EQ(client->music.current_source, WC3_MUSIC_SOURCE_MAP);
    T_STREQ(client->music.current_name, "OldMapMusic");
    T_EQ(client->music.current_index, 2);
    T_ASSERT(old_session != 0);

    G_MusicSetMap("NewMapMusic", true, 0);
    new_session = client->music.map_session_id;
    T_STREQ(client->music.map_name, "NewMapMusic");
    T_ASSERT(client->music.map_random);
    T_STREQ(client->music.current_name, "OldMapMusic");
    T_EQ(client->music.current_session_id, old_session);
    T_ASSERT(new_session != 0 && new_session != old_session);

    G_MusicMapTransitionFinished(client);
    T_STREQ(client->music.current_name, "NewMapMusic");
    T_ASSERT(client->music.current_random);
    T_EQ(client->music.current_index, 0);
    T_EQ(client->music.current_session_id, new_session);
    client->music = saved;
    currentplayer = previous;
}

TEST(wc3_music, clear_map_commits_silence_after_current_track_finishes) {
    gameClient_t * client = &game.clients[0];
    player_t * previous = currentplayer;
    wc3MusicState_t saved = client->music;

    currentplayer = &client->ps;
    memset(&client->music, 0, sizeof(client->music));
    G_MusicSetMap("MapMusic", false, 0);
    G_MusicClearMap();

    T_EQ(client->music.current_source, WC3_MUSIC_SOURCE_MAP);
    T_STREQ(client->music.current_name, "MapMusic");
    T_STREQ(client->music.map_name, "");
    T_EQ(client->music.map_session_id, 0);

    G_MusicMapTransitionFinished(client);
    T_EQ(client->music.current_source, WC3_MUSIC_SOURCE_NONE);
    T_STREQ(client->music.current_name, "");
    T_EQ(client->music.current_session_id, 0);
    client->music = saved;
    currentplayer = previous;
}

TEST(wc3_music, selected_random_initial_track_becomes_sequential_current_state) {
    gameClient_t * client = &game.clients[0];
    player_t * previous = currentplayer;
    wc3MusicState_t saved = client->music;
    uint32_t session_id;

    currentplayer = &client->ps;
    memset(&client->music, 0, sizeof(client->music));
    G_MusicSetMap("A;B;C", true, 0);
    session_id = client->music.current_session_id;
    T_ASSERT(client->music.current_random);

    G_MusicTrackSelected(client, session_id, 2, 0, 1u << 2);
    T_ASSERT(!client->music.current_random);
    T_EQ(client->music.current_index, 2);
    T_EQ(client->music.current_played_mask, 1u << 2);
    T_ASSERT(client->music.map_random);
    T_EQ(client->music.map_index, 0);

    client->music = saved;
    currentplayer = previous;
}

TEST(wc3_music, explicit_music_completion_returns_to_map_session) {
    gameClient_t * client = &game.clients[0];
    player_t * previous = currentplayer;
    wc3MusicState_t saved = client->music;
    uint32_t map_session, explicit_session;

    currentplayer = &client->ps;
    memset(&client->music, 0, sizeof(client->music));
    G_MusicSetMap("MapMusic", true, 0);
    map_session = client->music.map_session_id;
    G_MusicPlay("ExplicitMusic", 0, 0);
    explicit_session = client->music.current_session_id;

    T_EQ(client->music.current_source, WC3_MUSIC_SOURCE_EXPLICIT);
    T_ASSERT(explicit_session != map_session);
    G_MusicExplicitFinished(client);
    T_EQ(client->music.current_source, WC3_MUSIC_SOURCE_MAP);
    T_STREQ(client->music.current_name, "MapMusic");
    T_EQ(client->music.current_session_id, map_session);

    client->music = saved;
    currentplayer = previous;
}

TEST(wc3_music, thematic_music_restores_explicit_session_and_snapshot_position) {
    gameClient_t * client = &game.clients[0];
    player_t * previous = currentplayer;
    wc3MusicState_t saved = client->music;
    uint32_t explicit_session, thematic_session;

    currentplayer = &client->ps;
    memset(&client->music, 0, sizeof(client->music));
    G_MusicPlay("ExplicitMusic", 12000, 750);
    explicit_session = client->music.current_session_id;
    G_MusicPlayThematic("StoryTheme", 3000);
    thematic_session = client->music.current_session_id;

    T_EQ(client->music.current_source, WC3_MUSIC_SOURCE_THEMATIC);
    T_ASSERT(client->music.thematic_restore.valid);
    T_EQ(client->music.thematic_restore.session_id, explicit_session);

    G_MusicThematicSnapshot(client, thematic_session, explicit_session, 3, 45678, (1u << 1) | (1u << 3));
    T_EQ(client->music.thematic_restore.index, 3);
    T_EQ(client->music.thematic_restore.position_ms, 45678);
    T_ASSERT(!client->music.thematic_restore.random);
    T_EQ(client->music.thematic_restore.played_mask, (1u << 1) | (1u << 3));

    G_MusicEndThematic();
    T_EQ(client->music.current_source, WC3_MUSIC_SOURCE_EXPLICIT);
    T_STREQ(client->music.current_name, "ExplicitMusic");
    T_EQ(client->music.current_session_id, explicit_session);
    T_EQ(client->music.current_index, 3);
    T_EQ(client->music.current_position_ms, 45678);
    T_EQ(client->music.current_played_mask, (1u << 1) | (1u << 3));
    client->music = saved;
    currentplayer = previous;
}

TEST(wc3_music, map_change_during_theme_preserves_interrupted_map_session) {
    gameClient_t * client = &game.clients[0];
    player_t * previous = currentplayer;
    wc3MusicState_t saved = client->music;
    uint32_t old_map_session, new_map_session;

    currentplayer = &client->ps;
    memset(&client->music, 0, sizeof(client->music));
    G_MusicSetMap("OldMapMusic", false, 1);
    old_map_session = client->music.current_session_id;
    G_MusicPlayThematic("StoryTheme", 0);
    G_MusicSetMap("NewMapMusic", true, 0);
    new_map_session = client->music.map_session_id;
    G_MusicEndThematic();

    T_EQ(client->music.current_source, WC3_MUSIC_SOURCE_MAP);
    T_STREQ(client->music.current_name, "OldMapMusic");
    T_EQ(client->music.current_session_id, old_map_session);
    T_STREQ(client->music.map_name, "NewMapMusic");
    T_EQ(client->music.map_session_id, new_map_session);
    T_ASSERT(new_map_session != old_map_session);
    client->music = saved;
    currentplayer = previous;
}

TEST(wc3_music, session_ids_reject_stale_client_completion) {
    gameClient_t * client = &game.clients[0];
    player_t * previous = currentplayer;
    wc3MusicState_t saved = client->music;
    uint32_t old_session, new_session;

    currentplayer = &client->ps;
    memset(&client->music, 0, sizeof(client->music));
    G_MusicSetMap("OldMapMusic", true, 0);
    old_session = client->music.current_session_id;
    G_MusicSetMap("NewMapMusic", false, 0);
    new_session = client->music.map_session_id;
    T_ASSERT(G_MusicAcceptFinished(client, old_session));
    G_MusicMapTransitionFinished(client);
    T_ASSERT(!G_MusicAcceptFinished(client, old_session));
    T_ASSERT(G_MusicAcceptFinished(client, new_session));

    G_MusicPlayThematic("ThemeA", 0);
    old_session = client->music.current_session_id;
    G_MusicPlayThematic("ThemeB", 0);
    T_ASSERT(!G_MusicAcceptFinished(client, old_session));
    T_ASSERT(G_MusicAcceptFinished(client, client->music.current_session_id));
    client->music = saved;
    currentplayer = previous;
}

TEST(wc3_music, map_skin_overrides_stock_music_skin_fields) {
    handle_t archive = NULL;
    uint32_t size = 0;
    handle_t bytes;
    gameClient_t client = { .ps.race = kPlayerRaceHuman };
    cstring_t expected_versioned;

    bytes = gi.ReadFile("Maps\\MapOverlay.w3x", &size);
    T_NOT_NULL(bytes);
    T_ASSERT(SFileOpenArchiveFromMemory(bytes, size, 0, &archive));
    gi.SetPriorityArchive(archive);
    T_ASSERT(Stb_IniCacheLoad(&game.config.map_skin, "war3mapSkin.txt"));
    T_STREQ(Theme_PlayerString(&client, "Music", "fallback"), "MapMusicOverride");

    expected_versioned = atoi(gi.CvarString("fs_expansion", "0")) != 0
        ? "MapMusicTFT"
        : "MapMusicROC";
    T_STREQ(Theme_PlayerString(&client, "VersionedMusic", "fallback"), expected_versioned);

    Stb_IniCacheFree(&game.config.map_skin);
    gi.SetPriorityArchive(NULL);
    SFileCloseArchive(archive);
    gi.MemFree(bytes);
}
TEST(wc3_music, sound_file_duration_reads_wav_metadata_without_decoder) {
    uint8_t wav[144] = { 0 };

    memcpy(wav, "RIFF", 4);
    wav[4] = 136;
    memcpy(wav + 8, "WAVEfmt ", 8);
    wav[16] = 16;
    wav[20] = 1; /* PCM */
    wav[22] = 1; /* mono */
    wav[24] = 100; /* 100 Hz */
    wav[28] = 100; /* 100 bytes/sec */
    wav[32] = 1; /* block align */
    wav[34] = 8; /* bits/sample */
    memcpy(wav + 36, "data", 4);
    wav[40] = 100;

    T_EQ(G_AudioDurationFromMemory("test.wav", wav, sizeof(wav)), 1000);
}

TEST(wc3_music, sound_file_duration_scans_variable_rate_mp3_frames) {
    uint8_t mp3[625] = { 0 };

    /* MPEG-1 Layer III, 128 kbps, 44.1 kHz: 417-byte frame, 1152 samples. */
    mp3[0] = 0xff; mp3[1] = 0xfb; mp3[2] = 0x90; mp3[3] = 0x00;
    /* The second frame is 64 kbps (208 bytes), so duration cannot use one bitrate. */
    mp3[417] = 0xff; mp3[418] = 0xfb; mp3[419] = 0x50; mp3[420] = 0x00;

    T_EQ(G_AudioDurationFromMemory("test.mp3", mp3, sizeof(mp3)), 52);
}

TEST(wc3_music, sound_file_duration_rejects_truncated_or_unknown_data) {
    uint8_t truncated_wav[12] = { 0 };
    uint8_t unknown[8] = { 0xde, 0xad, 0xbe, 0xef };

    memcpy(truncated_wav, "RIFFWAVE", 8);
    T_EQ(G_AudioDurationFromMemory("truncated.wav", truncated_wav, sizeof(truncated_wav)), 0);
    T_EQ(G_AudioDurationFromMemory("unknown.bin", unknown, sizeof(unknown)), 0);
}

TEST(wc3_music, sound_file_duration_reads_ogg_vorbis_granule) {
    uint8_t ogg[58] = { 0 };
    uint8_t *packet = ogg + 28;

    memcpy(ogg, "OggS", 4);
    ogg[6] = 0x44; ogg[7] = 0xac; /* granule = 44100 samples */
    ogg[26] = 1;
    ogg[27] = 30;
    packet[0] = 1;
    memcpy(packet + 1, "vorbis", 6);
    packet[12] = 0x44; packet[13] = 0xac; /* sample rate = 44100 Hz */

    T_EQ(G_AudioDurationFromMemory("test.ogg", ogg, sizeof(ogg)), 1000);
}

TEST(wc3_music, sound_file_duration_reads_flac_streaminfo) {
    uint8_t flac[42] = { 0 };

    memcpy(flac, "fLaC", 4);
    flac[4] = 0x80; /* final STREAMINFO block */
    flac[7] = 34;
    flac[18] = 0x0a; flac[19] = 0xc4; flac[20] = 0x40; /* 44100 Hz */
    flac[24] = 0xac; flac[25] = 0x44; /* 44100 total samples */

    T_EQ(G_AudioDurationFromMemory("test.flac", flac, sizeof(flac)), 1000);
}

#endif
