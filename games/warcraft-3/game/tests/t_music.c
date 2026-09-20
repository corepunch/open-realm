#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"

extern LPPLAYER currentplayer;

TEST(wc3_music, set_map_defers_replacement_until_current_map_track_finishes) {
    LPGAMECLIENT client = &game.clients[0];
    LPPLAYER previous = currentplayer;
    wc3MusicState_t saved = client->music;
    DWORD old_session, new_session;

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
    LPGAMECLIENT client = &game.clients[0];
    LPPLAYER previous = currentplayer;
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
    LPGAMECLIENT client = &game.clients[0];
    LPPLAYER previous = currentplayer;
    wc3MusicState_t saved = client->music;
    DWORD session_id;

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
    LPGAMECLIENT client = &game.clients[0];
    LPPLAYER previous = currentplayer;
    wc3MusicState_t saved = client->music;
    DWORD map_session, explicit_session;

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
    LPGAMECLIENT client = &game.clients[0];
    LPPLAYER previous = currentplayer;
    wc3MusicState_t saved = client->music;
    DWORD explicit_session, thematic_session;

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
    LPGAMECLIENT client = &game.clients[0];
    LPPLAYER previous = currentplayer;
    wc3MusicState_t saved = client->music;
    DWORD old_map_session, new_map_session;

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
    LPGAMECLIENT client = &game.clients[0];
    LPPLAYER previous = currentplayer;
    wc3MusicState_t saved = client->music;
    DWORD old_session, new_session;

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
    HANDLE archive = NULL;
    DWORD size = 0;
    HANDLE bytes;
    GAMECLIENT client = { .ps.race = kPlayerRaceHuman };
    LPCSTR expected_versioned;

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
#endif
