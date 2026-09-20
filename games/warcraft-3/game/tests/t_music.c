#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"

extern LPPLAYER currentplayer;

TEST(wc3_music, set_map_defers_replacement_until_current_map_track_finishes) {
    LPGAMECLIENT client = &game.clients[0];
    LPPLAYER previous = currentplayer;
    wc3MusicState_t saved = client->music;

    currentplayer = &client->ps;
    memset(&client->music, 0, sizeof(client->music));
    G_MusicSetMap("OldMapMusic", false, 2);
    T_EQ(client->music.current_source, WC3_MUSIC_SOURCE_MAP);
    T_STREQ(client->music.current_name, "OldMapMusic");
    T_EQ(client->music.current_index, 2);

    G_MusicSetMap("NewMapMusic", true, 0);
    T_STREQ(client->music.map_name, "NewMapMusic");
    T_ASSERT(client->music.map_random);
    T_STREQ(client->music.current_name, "OldMapMusic");
    T_ASSERT(!client->music.current_random);
    T_EQ(client->music.current_index, 2);

    /* Client EOF acknowledgement commits the pending default into retained
     * server state for save/load and reconnect synchronization. */
    G_MusicMapTransitionFinished(client);
    T_STREQ(client->music.current_name, "NewMapMusic");
    T_ASSERT(client->music.current_random);
    T_EQ(client->music.current_index, 0);
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

    G_MusicMapTransitionFinished(client);
    T_EQ(client->music.current_source, WC3_MUSIC_SOURCE_NONE);
    T_STREQ(client->music.current_name, "");
    client->music = saved;
    currentplayer = previous;
}

TEST(wc3_music, thematic_music_restores_explicit_session) {
    LPGAMECLIENT client = &game.clients[0];
    LPPLAYER previous = currentplayer;
    wc3MusicState_t saved = client->music;

    currentplayer = &client->ps;
    memset(&client->music, 0, sizeof(client->music));
    G_MusicPlay("ExplicitMusic", 12000, 750);
    G_MusicPlayThematic("StoryTheme", 3000);

    T_EQ(client->music.current_source, WC3_MUSIC_SOURCE_THEMATIC);
    G_MusicEndThematic();
    T_EQ(client->music.current_source, WC3_MUSIC_SOURCE_EXPLICIT);
    T_STREQ(client->music.current_name, "ExplicitMusic");
    T_EQ(client->music.current_position_ms, 12000);
    client->music = saved;
    currentplayer = previous;
}

TEST(wc3_music, map_change_during_theme_preserves_interrupted_map_session) {
    LPGAMECLIENT client = &game.clients[0];
    LPPLAYER previous = currentplayer;
    wc3MusicState_t saved = client->music;

    currentplayer = &client->ps;
    memset(&client->music, 0, sizeof(client->music));
    G_MusicSetMap("OldMapMusic", false, 1);
    G_MusicPlayThematic("StoryTheme", 0);
    G_MusicSetMap("NewMapMusic", true, 0);
    G_MusicEndThematic();

    T_EQ(client->music.current_source, WC3_MUSIC_SOURCE_MAP);
    T_STREQ(client->music.current_name, "OldMapMusic");
    T_STREQ(client->music.map_name, "NewMapMusic");
    T_ASSERT(client->music.map_random);
    client->music = saved;
    currentplayer = previous;
}

TEST(wc3_music, stale_client_completion_cannot_commit_new_session) {
    LPGAMECLIENT client = &game.clients[0];
    LPPLAYER previous = currentplayer;
    wc3MusicState_t saved = client->music;
    DWORD old_token;

    currentplayer = &client->ps;
    memset(&client->music, 0, sizeof(client->music));
    G_MusicSetMap("OldMapMusic", false, 0);
    old_token = BZ_MusicSessionToken("OldMapMusic", WC3_MUSIC_SOURCE_MAP, 0);
    G_MusicSetMap("NewMapMusic", false, 0);
    T_ASSERT(G_MusicAcceptFinished(client, WC3_MUSIC_SOURCE_MAP, old_token));
    G_MusicMapTransitionFinished(client);
    T_ASSERT(!G_MusicAcceptFinished(client, WC3_MUSIC_SOURCE_MAP, old_token));

    G_MusicPlayThematic("ThemeA", 0);
    old_token = BZ_MusicSessionToken("ThemeA", WC3_MUSIC_SOURCE_THEMATIC, 0);
    G_MusicPlayThematic("ThemeB", 0);
    T_ASSERT(!G_MusicAcceptFinished(client, WC3_MUSIC_SOURCE_THEMATIC, old_token));
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
