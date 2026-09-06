#include "g_local.h"
#include "g_unitrow.h"

extern LPPLAYER currentplayer;

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
                              BOOL random, LONG index, LONG start_ms, LONG fade_ms) {
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
        gi.Write(PF_LONG, &start_ms);
        gi.Write(PF_LONG, &fade_ms);
    } else {
        gi.Write(PF_LONG, &start_ms);
    }
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
                              BOOL random, LONG index, LONG position_ms, LONG fade_ms) {
    wc3MusicState_t *state = &client->music;

    strlcpy(state->current_name, music_name ? music_name : "", sizeof(state->current_name));
    state->current_source = state->current_name[0] ? source : WC3_MUSIC_SOURCE_NONE;
    state->current_random = random;
    state->current_index = MAX(0, index);
    state->current_position_ms = MAX(0, position_ms);
    state->current_fade_ms = MAX(0, fade_ms);
    state->paused = false;
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

    if (!client || !client->connected) return;
    state = &client->music;

    G_MusicWriteSimple(client, MUSIC_CMD_SET_VOLUME, state->volume);
    G_MusicWriteSimple(client, MUSIC_CMD_SET_THEMATIC_VOLUME, state->thematic_volume);

    if (state->map_name[0]) {
        G_MusicWriteNamed(client, MUSIC_CMD_SET_MAP, state->map_name,
                          state->map_random, state->map_index, 0, 0);
    }

    switch (state->current_source) {
        case WC3_MUSIC_SOURCE_MAP:
            /* SET_MAP already starts the map playlist on a fresh client. If
             * ClearMapMusic removed only the stored default while that map
             * playlist was audible, recreate the retained playlist with its
             * original selection policy, then clear only the default again. */
            if (!state->map_name[0] && state->current_name[0]) {
                G_MusicWriteNamed(client, MUSIC_CMD_SET_MAP, state->current_name,
                                  state->current_random, state->current_index, 0, 0);
                G_MusicWriteSimple(client, MUSIC_CMD_CLEAR_MAP, 0);
            }
            if (state->current_position_ms > 0)
                G_MusicWriteSimple(client, MUSIC_CMD_SET_POSITION, state->current_position_ms);
            break;
        case WC3_MUSIC_SOURCE_EXPLICIT:
            G_MusicWriteNamed(client, MUSIC_CMD_PLAY, state->current_name,
                              true, 0, state->current_position_ms, 0);
            break;
        case WC3_MUSIC_SOURCE_THEMATIC:
            G_MusicWriteNamed(client, MUSIC_CMD_PLAY_THEMATIC, state->current_name,
                              false, 0, state->current_position_ms, 0);
            break;
        default:
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

    strlcpy(state->map_name, ctx->name ? ctx->name : "", sizeof(state->map_name));
    state->map_random = ctx->random;
    state->map_index = MAX(0, ctx->index);
    if (state->current_source == WC3_MUSIC_SOURCE_NONE || state->current_source == WC3_MUSIC_SOURCE_MAP) {
        G_MusicSetCurrent(client, WC3_MUSIC_SOURCE_MAP, state->map_name,
                          state->map_random, state->map_index, 0, 0);
    }
    G_MusicWriteNamed(client, MUSIC_CMD_SET_MAP, state->map_name,
                      state->map_random, state->map_index, 0, 0);
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
    G_MusicSetCurrent(client, WC3_MUSIC_SOURCE_EXPLICIT, ctx->name, true, 0, ctx->start_ms, ctx->fade_ms);
    G_MusicWriteNamed(client, MUSIC_CMD_PLAY, ctx->name, true, 0, ctx->start_ms, ctx->fade_ms);
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
    G_MusicSetCurrent(client, WC3_MUSIC_SOURCE_THEMATIC, ctx->name, false, 0, ctx->start_ms, 0);
    G_MusicWriteNamed(client, MUSIC_CMD_PLAY_THEMATIC, ctx->name, false, 0, ctx->start_ms, 0);
}

void G_MusicPlayThematic(LPCSTR music_name, LONG start_ms) {
    musicPlayContext_t context = { music_name, MAX(0, start_ms), 0 };
    G_MusicForRecipients(G_MusicPlayThematicClient, &context);
}

static void G_MusicEndThematicClient(LPGAMECLIENT client, void *context) {
    wc3MusicState_t *state = &client->music;
    (void)context;

    if (state->current_source == WC3_MUSIC_SOURCE_THEMATIC) {
        if (state->map_name[0]) {
            G_MusicSetCurrent(client, WC3_MUSIC_SOURCE_MAP, state->map_name,
                              state->map_random, state->map_index, 0, 0);
        } else {
            G_MusicSetCurrent(client, WC3_MUSIC_SOURCE_NONE, "", false, 0, 0, 0);
        }
    }
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
