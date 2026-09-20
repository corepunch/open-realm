#include "g_local.h"
#include "g_unitrow.h"

extern LPPLAYER currentplayer;

static DWORD G_MusicNextSession(LPGAMECLIENT client) {
    wc3MusicState_t *state;

    if (!client) return 0;
    state = &client->music;
    state->session_serial++;
    if (!state->session_serial || state->session_serial > 0x7fffffffu) state->session_serial = 1;
    return state->session_serial;
}

static wc3MusicRestore_t *G_MusicRestore(LPGAMECLIENT client) {
    return client ? &client->music.thematic_restore : NULL;
}

static void G_MusicTrimToken(LPCSTR start, size_t length, LPSTR out, size_t out_size) {
    LPCSTR end = start + length;

    if (!out || !out_size) return;
    while (start < end && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')) start++;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) end--;
    snprintf(out, out_size, "%.*s", (int)MIN((size_t)(end - start), out_size - 1), start);
}

static void G_MusicAppend(LPSTR out, size_t out_size, LPCSTR value) {
    size_t used;

    if (!out || !out_size || !value || !*value) return;
    used = strlen(out);
    if (used && used + 1 < out_size) {
        out[used++] = ';';
        out[used] = '\0';
    }
    if (used + 1 < out_size) strlcpy(out + used, value, out_size - used);
}

/* Resolve the skin alias for this recipient, then expand Music.SLK aliases.
 * Music.SLK FileNames may itself contain comma-separated tracks; the client
 * deliberately owns the final playlist split and playback order. */
static void G_MusicResolvePlaylist(LPGAMECLIENT client, LPCSTR music_name, LPSTR out, size_t out_size) {
    LPCSTR resolved;
    LPCSTR cursor;

    if (!out || !out_size) return;
    out[0] = '\0';
    if (!music_name || !*music_name) return;

    resolved = Theme_PlayerString(client, music_name, music_name);
    cursor = resolved ? resolved : music_name;
    while (*cursor) {
        LPCSTR separator = strchr(cursor, ';');
        size_t length = separator ? (size_t)(separator - cursor) : strlen(cursor);
        char token[MAX_PATHLEN];
        MusicData_t const *row;

        G_MusicTrimToken(cursor, length, token, sizeof(token));
        if (token[0]) {
            row = G_MusicData(token);
            G_MusicAppend(out, out_size, row && row->FileNames && row->FileNames[0] ? row->FileNames : token);
        }
        if (!separator) break;
        cursor = separator + 1;
    }
}

static LPEDICT G_MusicRecipientEntity(LPGAMECLIENT client) {
    if (!client || !client->connected) return NULL;
    return G_GetPlayerEntityByNumber(client->ps.number);
}

static void G_MusicWriteNamed(LPGAMECLIENT client, musicCommand_t command, LPCSTR music_name,
                              BOOL random, LONG index, LONG start_ms, LONG fade_ms,
                              DWORD played_mask, DWORD session_id) {
    LPEDICT recipient;
    char playlist[WC3_MUSIC_NAME_MAX];

    if (command != MUSIC_CMD_SET_MAP && command != MUSIC_CMD_PLAY && command != MUSIC_CMD_PLAY_THEMATIC) return;
    recipient = G_MusicRecipientEntity(client);
    if (!recipient) return;

    G_MusicResolvePlaylist(client, music_name, playlist, sizeof(playlist));
    if (!playlist[0]) return;

    gi.Write(PF_BYTE, &(LONG){ svc_music });
    gi.Write(PF_BYTE, &(LONG){ command });
    if (command == MUSIC_CMD_SET_MAP) {
        gi.Write(PF_BYTE, &(LONG){ random ? 1 : 0 });
        gi.Write(PF_LONG, &index);
    } else if (command == MUSIC_CMD_PLAY) {
        gi.Write(PF_BYTE, &(LONG){ random ? 1 : 0 });
        gi.Write(PF_LONG, &index);
        gi.Write(PF_LONG, &start_ms);
        gi.Write(PF_LONG, &fade_ms);
        gi.Write(PF_LONG, &played_mask);
    } else {
        gi.Write(PF_LONG, &index);
        gi.Write(PF_LONG, &start_ms);
    }
    gi.Write(PF_LONG, &session_id);
    gi.Write(PF_STRING, playlist);
    gi.unicast(recipient);
}

static void G_MusicWriteSimple(LPGAMECLIENT client, musicCommand_t command, LONG value) {
    LPEDICT recipient;

    switch (command) {
        case MUSIC_CMD_STOP:
        case MUSIC_CMD_SET_VOLUME:
        case MUSIC_CMD_SET_POSITION:
        case MUSIC_CMD_SET_THEMATIC_VOLUME:
        case MUSIC_CMD_SET_THEMATIC_POSITION:
        case MUSIC_CMD_CLEAR_MAP:
        case MUSIC_CMD_RESUME:
        case MUSIC_CMD_END_THEMATIC:
            break;
        default:
            return;
    }

    recipient = G_MusicRecipientEntity(client);
    if (!recipient) return;
    gi.Write(PF_BYTE, &(LONG){ svc_music });
    gi.Write(PF_BYTE, &(LONG){ command });
    switch (command) {
        case MUSIC_CMD_STOP:
            gi.Write(PF_BYTE, &(LONG){ value ? 1 : 0 });
            break;
        case MUSIC_CMD_SET_VOLUME:
        case MUSIC_CMD_SET_POSITION:
        case MUSIC_CMD_SET_THEMATIC_VOLUME:
        case MUSIC_CMD_SET_THEMATIC_POSITION:
            gi.Write(PF_LONG, &value);
            break;
        default:
            break;
    }
    gi.unicast(recipient);
}

static void G_MusicSetCurrent(LPGAMECLIENT client, wc3MusicSource_t source, LPCSTR music_name,
                              BOOL random, LONG index, LONG position_ms, LONG fade_ms,
                              DWORD played_mask, DWORD session_id) {
    wc3MusicState_t *state = &client->music;

    strlcpy(state->current_name, music_name ? music_name : "", sizeof(state->current_name));
    state->current_source = state->current_name[0] ? source : WC3_MUSIC_SOURCE_NONE;
    state->current_random = random;
    state->current_index = MAX(0, index);
    state->current_position_ms = MAX(0, position_ms);
    state->current_fade_ms = MAX(0, fade_ms);
    state->current_played_mask = played_mask;
    state->current_session_id = state->current_source == WC3_MUSIC_SOURCE_NONE ? 0 : session_id;
    state->paused = false;
}

static void G_MusicClearThematicPrevious(LPGAMECLIENT client) {
    wc3MusicRestore_t *restore = G_MusicRestore(client);
    if (restore) memset(restore, 0, sizeof(*restore));
}

static void G_MusicRememberThematicPrevious(LPGAMECLIENT client) {
    wc3MusicState_t *state = client ? &client->music : NULL;
    wc3MusicRestore_t *restore = G_MusicRestore(client);
    if (!restore || !state || state->current_source == WC3_MUSIC_SOURCE_NONE ||
        state->current_source == WC3_MUSIC_SOURCE_THEMATIC || !state->current_name[0]) {
        G_MusicClearThematicPrevious(client);
        return;
    }
    strlcpy(restore->name, state->current_name, sizeof(restore->name));
    restore->source = state->current_source;
    restore->random = state->current_random;
    restore->index = state->current_index;
    restore->position_ms = state->current_position_ms;
    restore->fade_ms = state->current_fade_ms;
    restore->played_mask = state->current_played_mask;
    restore->paused = state->paused;
    restore->session_id = state->current_session_id;
    restore->valid = true;
}

static BOOL G_MusicRestoreThematicPrevious(LPGAMECLIENT client) {
    wc3MusicState_t *state = client ? &client->music : NULL;
    wc3MusicRestore_t *restore = G_MusicRestore(client);
    if (!restore || !state || !restore->valid || !restore->name[0]) return false;
    strlcpy(state->current_name, restore->name, sizeof(state->current_name));
    state->current_source = restore->source;
    state->current_random = restore->random;
    state->current_index = restore->index;
    state->current_position_ms = restore->position_ms;
    state->current_fade_ms = restore->fade_ms;
    state->current_played_mask = restore->played_mask;
    state->paused = restore->paused;
    state->current_session_id = restore->session_id;
    G_MusicClearThematicPrevious(client);
    return true;
}

static BOOL G_MusicMapMatchesCurrent(wc3MusicState_t const *state) {
    return state && state->current_source == WC3_MUSIC_SOURCE_MAP && state->current_session_id &&
        state->current_session_id == state->map_session_id;
}

BOOL G_MusicAcceptFinished(LPGAMECLIENT client, DWORD session_id) {
    return client && session_id && session_id == client->music.current_session_id;
}

void G_MusicTrackSelected(LPGAMECLIENT client, DWORD session_id, LONG index, LONG position_ms, DWORD played_mask) {
    wc3MusicState_t *state;

    if (!client || !session_id) return;
    state = &client->music;
    if (session_id != state->current_session_id || state->current_source == WC3_MUSIC_SOURCE_NONE) return;
    state->current_random = false;
    state->current_index = MAX(0, index);
    state->current_position_ms = MAX(0, position_ms);
    state->current_played_mask = played_mask;
}

void G_MusicThematicSnapshot(LPGAMECLIENT client, DWORD thematic_session_id, DWORD restore_session_id,
                             LONG index, LONG position_ms, DWORD played_mask) {
    wc3MusicState_t *state;
    wc3MusicRestore_t *restore;

    if (!client || !thematic_session_id || !restore_session_id) return;
    state = &client->music;
    restore = G_MusicRestore(client);
    if (state->current_source != WC3_MUSIC_SOURCE_THEMATIC ||
        state->current_session_id != thematic_session_id || !restore || !restore->valid ||
        restore->session_id != restore_session_id) return;
    restore->random = false;
    restore->index = MAX(0, index);
    restore->position_ms = MAX(0, position_ms);
    restore->played_mask = played_mask;
}

static void G_MusicForRecipients(void (*callback)(LPGAMECLIENT client, void *context), void *context) {
    if (currentplayer) {
        LPGAMECLIENT client = PLAYER_CLIENT(currentplayer);
        if (client) callback(client, context);
        return;
    }
    FOR_LOOP(i, game.max_clients) callback(game.clients + i, context);
}

void G_MusicResetState(void) {
    FOR_LOOP(i, game.max_clients) {
        memset(&game.clients[i].music, 0, sizeof(game.clients[i].music));
        game.clients[i].music.volume = 127;
        game.clients[i].music.thematic_volume = 127;
    }
}

void G_MusicSyncClient(LPGAMECLIENT client) {
    wc3MusicState_t *state;
    wc3MusicRestore_t *restore;

    if (!client || !client->connected) return;
    state = &client->music;
    restore = G_MusicRestore(client);

    G_MusicWriteSimple(client, MUSIC_CMD_SET_VOLUME, state->volume);
    G_MusicWriteSimple(client, MUSIC_CMD_SET_THEMATIC_VOLUME, state->thematic_volume);

    switch (state->current_source) {
        case WC3_MUSIC_SOURCE_MAP:
            if (state->current_name[0]) {
                G_MusicWriteNamed(client, MUSIC_CMD_SET_MAP, state->current_name,
                                  state->current_random, state->current_index, 0, 0,
                                  state->current_played_mask, state->current_session_id);
                /* Restore the persistent map-list policy after selecting the exact
                 * retained current track.  The same session id means this is not a
                 * pending replacement; a different id is the deferred next list. */
                if (state->map_name[0])
                    G_MusicWriteNamed(client, MUSIC_CMD_SET_MAP, state->map_name,
                                      state->map_random, state->map_index, 0, 0,
                                      0, state->map_session_id);
                else if (!G_MusicMapMatchesCurrent(state))
                    G_MusicWriteSimple(client, MUSIC_CMD_CLEAR_MAP, 0);
            } else if (state->map_name[0]) {
                G_MusicWriteNamed(client, MUSIC_CMD_SET_MAP, state->map_name,
                                  state->map_random, state->map_index, 0, 0,
                                  0, state->map_session_id);
            }
            if (state->current_position_ms > 0)
                G_MusicWriteSimple(client, MUSIC_CMD_SET_POSITION, state->current_position_ms);
            break;

        case WC3_MUSIC_SOURCE_EXPLICIT:
            if (state->map_name[0])
                G_MusicWriteNamed(client, MUSIC_CMD_SET_MAP, state->map_name,
                                  state->map_random, state->map_index, 0, 0,
                                  0, state->map_session_id);
            G_MusicWriteNamed(client, MUSIC_CMD_PLAY, state->current_name,
                              state->current_random, state->current_index,
                              state->current_position_ms, 0, state->current_played_mask,
                              state->current_session_id);
            break;

        case WC3_MUSIC_SOURCE_THEMATIC:
            /* Rebuild the interrupted ordinary session first.  The client snapshots
             * the exact audible position when the theme begins and reports it back,
             * so this restore descriptor is also valid across save/load. */
            if (restore && restore->valid && restore->source == WC3_MUSIC_SOURCE_MAP && restore->name[0]) {
                G_MusicWriteNamed(client, MUSIC_CMD_SET_MAP, restore->name,
                                  restore->random, restore->index, 0, 0, 0, restore->session_id);
                if (state->map_name[0])
                    G_MusicWriteNamed(client, MUSIC_CMD_SET_MAP, state->map_name,
                                      state->map_random, state->map_index, 0, 0,
                                      0, state->map_session_id);
                else if (restore->session_id != state->map_session_id)
                    G_MusicWriteSimple(client, MUSIC_CMD_CLEAR_MAP, 0);
                if (restore->position_ms > 0)
                    G_MusicWriteSimple(client, MUSIC_CMD_SET_POSITION, restore->position_ms);
                if (restore->paused)
                    G_MusicWriteSimple(client, MUSIC_CMD_STOP, 0);
            } else {
                if (state->map_name[0])
                    G_MusicWriteNamed(client, MUSIC_CMD_SET_MAP, state->map_name,
                                      state->map_random, state->map_index, 0, 0,
                                      0, state->map_session_id);
                if (restore && restore->valid && restore->source == WC3_MUSIC_SOURCE_EXPLICIT && restore->name[0]) {
                    G_MusicWriteNamed(client, MUSIC_CMD_PLAY, restore->name,
                                      restore->random, restore->index,
                                      restore->position_ms, 0, restore->played_mask, restore->session_id);
                    if (restore->paused)
                        G_MusicWriteSimple(client, MUSIC_CMD_STOP, 0);
                }
            }
            G_MusicWriteNamed(client, MUSIC_CMD_PLAY_THEMATIC, state->current_name,
                              false, state->current_index, state->current_position_ms, 0,
                              0, state->current_session_id);
            break;

        default:
            if (state->map_name[0])
                G_MusicWriteNamed(client, MUSIC_CMD_SET_MAP, state->map_name,
                                  state->map_random, state->map_index, 0, 0,
                                  0, state->map_session_id);
            break;
    }

    if (state->paused && state->current_source != WC3_MUSIC_SOURCE_NONE)
        G_MusicWriteSimple(client, MUSIC_CMD_STOP, 0);
}

typedef struct {
    LPCSTR name;
    BOOL random;
    LONG index;
} musicSetMapContext_t;

static void G_MusicSetMapClient(LPGAMECLIENT client, void *context) {
    musicSetMapContext_t const *ctx = context;
    wc3MusicState_t *state = &client->music;
    BOOL changed = strcmp(state->map_name, ctx->name ? ctx->name : "") ||
        state->map_random != ctx->random || state->map_index != MAX(0, ctx->index);

    strlcpy(state->map_name, ctx->name ? ctx->name : "", sizeof(state->map_name));
    state->map_random = ctx->random;
    state->map_index = MAX(0, ctx->index);
    if (changed || !state->map_session_id) state->map_session_id = G_MusicNextSession(client);

    /* SetMapMusic changes the default list, but an already audible map track
     * is allowed to finish before the new list takes over. */
    if (state->current_source == WC3_MUSIC_SOURCE_NONE) {
        G_MusicSetCurrent(client, WC3_MUSIC_SOURCE_MAP, state->map_name,
                          state->map_random, state->map_index, 0, 0, 0, state->map_session_id);
        G_MusicClearThematicPrevious(client);
    }
    G_MusicWriteNamed(client, MUSIC_CMD_SET_MAP, state->map_name,
                      state->map_random, state->map_index, 0, 0, 0, state->map_session_id);
}

void G_MusicSetMap(LPCSTR music_name, BOOL random, LONG index) {
    musicSetMapContext_t context = { music_name, random, index };
    G_MusicForRecipients(G_MusicSetMapClient, &context);
}

static void G_MusicClearMapClient(LPGAMECLIENT client, void *context) {
    (void)context;
    memset(client->music.map_name, 0, sizeof(client->music.map_name));
    client->music.map_random = false;
    client->music.map_index = 0;
    client->music.map_session_id = 0;
    G_MusicWriteSimple(client, MUSIC_CMD_CLEAR_MAP, 0);
}

void G_MusicClearMap(void) {
    G_MusicForRecipients(G_MusicClearMapClient, NULL);
}

typedef struct {
    LPCSTR name;
    LONG start_ms;
    LONG fade_ms;
} musicPlayContext_t;

static void G_MusicPlayClient(LPGAMECLIENT client, void *context) {
    musicPlayContext_t const *ctx = context;
    DWORD session_id = G_MusicNextSession(client);

    G_MusicClearThematicPrevious(client);
    G_MusicSetCurrent(client, WC3_MUSIC_SOURCE_EXPLICIT, ctx->name, true, 0,
                      ctx->start_ms, ctx->fade_ms, 0, session_id);
    G_MusicWriteNamed(client, MUSIC_CMD_PLAY, ctx->name, true, 0,
                      ctx->start_ms, ctx->fade_ms, 0, session_id);
}

void G_MusicPlay(LPCSTR music_name, LONG start_ms, LONG fade_ms) {
    musicPlayContext_t context = { music_name, MAX(0, start_ms), MAX(0, fade_ms) };
    G_MusicForRecipients(G_MusicPlayClient, &context);
}

static void G_MusicStopClient(LPGAMECLIENT client, void *context) {
    BOOL fade_out = *(BOOL *)context;
    if (client->music.current_source != WC3_MUSIC_SOURCE_NONE) client->music.paused = true;
    G_MusicWriteSimple(client, MUSIC_CMD_STOP, fade_out);
}

void G_MusicStop(BOOL fade_out) {
    G_MusicForRecipients(G_MusicStopClient, &fade_out);
}

static void G_MusicResumeClient(LPGAMECLIENT client, void *context) {
    (void)context;
    if (client->music.current_source != WC3_MUSIC_SOURCE_NONE) client->music.paused = false;
    G_MusicWriteSimple(client, MUSIC_CMD_RESUME, 0);
}

void G_MusicResume(void) {
    G_MusicForRecipients(G_MusicResumeClient, NULL);
}

static void G_MusicPlayThematicClient(LPGAMECLIENT client, void *context) {
    musicPlayContext_t const *ctx = context;
    DWORD session_id;

    if (client->music.current_source != WC3_MUSIC_SOURCE_THEMATIC)
        G_MusicRememberThematicPrevious(client);
    session_id = G_MusicNextSession(client);
    G_MusicSetCurrent(client, WC3_MUSIC_SOURCE_THEMATIC, ctx->name, false, 0,
                      ctx->start_ms, 0, 0, session_id);
    G_MusicWriteNamed(client, MUSIC_CMD_PLAY_THEMATIC, ctx->name, false, 0,
                      ctx->start_ms, 0, 0, session_id);
}

void G_MusicPlayThematic(LPCSTR music_name, LONG start_ms) {
    musicPlayContext_t context = { music_name, MAX(0, start_ms), 0 };
    G_MusicForRecipients(G_MusicPlayThematicClient, &context);
}

void G_MusicMapTransitionFinished(LPGAMECLIENT client) {
    wc3MusicState_t *state;

    if (!client) return;
    state = &client->music;
    if (state->current_source != WC3_MUSIC_SOURCE_MAP) return;
    if (state->map_name[0]) {
        G_MusicSetCurrent(client, WC3_MUSIC_SOURCE_MAP, state->map_name,
                          state->map_random, state->map_index, 0, 0, 0, state->map_session_id);
    } else {
        G_MusicSetCurrent(client, WC3_MUSIC_SOURCE_NONE, "", false, 0, 0, 0, 0, 0);
    }
}

void G_MusicExplicitFinished(LPGAMECLIENT client) {
    wc3MusicState_t *state;

    if (!client) return;
    state = &client->music;
    if (state->current_source != WC3_MUSIC_SOURCE_EXPLICIT) return;
    if (state->map_name[0]) {
        G_MusicSetCurrent(client, WC3_MUSIC_SOURCE_MAP, state->map_name,
                          state->map_random, state->map_index, 0, 0, 0, state->map_session_id);
    } else {
        G_MusicSetCurrent(client, WC3_MUSIC_SOURCE_NONE, "", false, 0, 0, 0, 0, 0);
    }
}

void G_MusicThematicFinished(LPGAMECLIENT client) {
    wc3MusicState_t *state;

    if (!client) return;
    state = &client->music;
    if (state->current_source != WC3_MUSIC_SOURCE_THEMATIC) return;
    if (!G_MusicRestoreThematicPrevious(client)) {
        if (state->map_name[0]) {
            G_MusicSetCurrent(client, WC3_MUSIC_SOURCE_MAP, state->map_name,
                              state->map_random, state->map_index, 0, 0, 0, state->map_session_id);
        } else {
            G_MusicSetCurrent(client, WC3_MUSIC_SOURCE_NONE, "", false, 0, 0, 0, 0, 0);
        }
        G_MusicClearThematicPrevious(client);
    }
}

static void G_MusicEndThematicClient(LPGAMECLIENT client, void *context) {
    (void)context;
    G_MusicThematicFinished(client);
    G_MusicWriteSimple(client, MUSIC_CMD_END_THEMATIC, 0);
}

void G_MusicEndThematic(void) {
    G_MusicForRecipients(G_MusicEndThematicClient, NULL);
}

typedef struct { LONG value; } musicValueContext_t;

static void G_MusicSetVolumeClient(LPGAMECLIENT client, void *context) {
    LONG value = ((musicValueContext_t *)context)->value;
    client->music.volume = value;
    G_MusicWriteSimple(client, MUSIC_CMD_SET_VOLUME, value);
}

void G_MusicSetVolume(LONG volume) {
    musicValueContext_t context = { MAX(0, MIN(volume, 127)) };
    G_MusicForRecipients(G_MusicSetVolumeClient, &context);
}

static void G_MusicSetPositionClient(LPGAMECLIENT client, void *context) {
    LONG value = ((musicValueContext_t *)context)->value;
    if (client->music.current_source != WC3_MUSIC_SOURCE_NONE)
        client->music.current_position_ms = value;
    G_MusicWriteSimple(client, MUSIC_CMD_SET_POSITION, value);
}

void G_MusicSetPosition(LONG millisecs) {
    musicValueContext_t context = { MAX(0, millisecs) };
    G_MusicForRecipients(G_MusicSetPositionClient, &context);
}

static void G_MusicSetThematicVolumeClient(LPGAMECLIENT client, void *context) {
    LONG value = ((musicValueContext_t *)context)->value;
    client->music.thematic_volume = value;
    G_MusicWriteSimple(client, MUSIC_CMD_SET_THEMATIC_VOLUME, value);
}

void G_MusicSetThematicVolume(LONG volume) {
    musicValueContext_t context = { MAX(0, MIN(volume, 127)) };
    G_MusicForRecipients(G_MusicSetThematicVolumeClient, &context);
}

static void G_MusicSetThematicPositionClient(LPGAMECLIENT client, void *context) {
    LONG value = ((musicValueContext_t *)context)->value;
    if (client->music.current_source == WC3_MUSIC_SOURCE_THEMATIC)
        client->music.current_position_ms = value;
    G_MusicWriteSimple(client, MUSIC_CMD_SET_THEMATIC_POSITION, value);
}

void G_MusicSetThematicPosition(LONG millisecs) {
    musicValueContext_t context = { MAX(0, millisecs) };
    G_MusicForRecipients(G_MusicSetThematicPositionClient, &context);
}
