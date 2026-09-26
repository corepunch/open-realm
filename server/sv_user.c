#include "server.h"
#include <stdlib.h>

static uint32_t SV_ClientPlayerNumber(client_t * cl) {
    return cl->playernum < MAX_PLAYERS ? cl->playernum : 0;
}

/* Continuation indexes come from the peer; reject malformed requests before indexing server tables. */
static int SV_SignonStart(int argc, cstring_t *argv, uint32_t count) {
    if (argc == 1) return 0;
    char *end;
    long start = argc == 2 ? strtol(argv[1], &end, 10) : -1;
    if (start < 0 || start > count || !argv[1][0] || *end) {
        fprintf(stderr, "SV_SignonStart: invalid %s continuation\n", argv[0]);
        return -1;
    }
    return (int)start;
}

/* Q2-style request pacing bounds UDP packets and avoids flooding the receiver during registration. */
void SV_Configstrings_f(client_t * cl, int argc, cstring_t *argv) {
    int start = SV_SignonStart(argc, argv, MAX_CONFIGSTRINGS);
    uint32_t limit = SV_SignonLimit(&cl->netchan);
    if (cl->state != cs_connected || start < 0) return;
    if (!cl->edict) cl->edict = EDICT_NUM(SV_ClientPlayerNumber(cl));
    for (; start < MAX_CONFIGSTRINGS; start++) {
        if (!*sv.configstrings[start]) continue;
        /* The engine buffer is much larger than a UDP datagram; reserve room for the next request too. */
        if (cl->netchan.message.cursize + SV_ConfigStringWireSize(start) + 32 > limit) break;
        SV_WriteConfigString(&cl->netchan.message, start);
    }
    char next[32];
    if (start == MAX_CONFIGSTRINGS) strlcpy(next, "baselines", sizeof(next));
    else snprintf(next, sizeof(next), "configstrings %d", start);
    MSG_WriteByte(&cl->netchan.message, svc_mirror);
    MSG_WriteString(&cl->netchan.message, next);
    Netchan_Transmit(NS_SERVER, &cl->netchan);
}

/* Baselines share the configstring continuation contract, so begin cannot overtake later entity pages. */
void SV_Baselines_f(client_t * cl, int argc, cstring_t *argv) {
    entityState_t empty = { 0 };
    int start = SV_SignonStart(argc, argv, ge->num_edicts);
    uint32_t limit = SV_SignonLimit(&cl->netchan);
    if (cl->state != cs_connected || start < 0) return;
    for (; start < ge->num_edicts; start++) {
        edict_t *ent = EDICT_NUM(start);
        if (ent->svflags & SVF_NOCLIENT) continue;
        if (cl->netchan.message.cursize + 512 + 32 > limit) break;
        MSG_WriteByte(&cl->netchan.message, svc_spawnbaseline);
        MSG_WriteDeltaEntity(&cl->netchan.message, &empty, &ent->s, true);
    }
    char next[32];
    if (start == ge->num_edicts) strlcpy(next, "precache", sizeof(next));
    else snprintf(next, sizeof(next), "baselines %d", start);
    MSG_WriteByte(&cl->netchan.message, svc_mirror);
    MSG_WriteString(&cl->netchan.message, next);
    Netchan_Transmit(NS_SERVER, &cl->netchan);
}

void SV_Begin_f(client_t * cl, int argc, cstring_t *argv) {
    uint32_t playernum;

    (void)argc;
    (void)argv;

    if (!cl->edict) {
        playernum = SV_ClientPlayerNumber(cl);
        cl->edict = EDICT_NUM(playernum);
        fprintf(stderr,
                "SV_Begin_f: assigned missing edict for client=%ld player=%u\n",
                (long)(cl - svs.clients),
                (unsigned)playernum);
    }
    cl->state = cs_spawned;
    cl->lastframe = (uint32_t)-1;
    ge->ClientBegin(cl->edict);
}

void SV_PlayerInfo_f(client_t * cl, int argc, cstring_t *argv) {
    uint32_t playernum;

    (void)argc;
    (void)argv;

    /* Assign the client's game edict (Quake 2/3 pattern) */
    playernum = SV_ClientPlayerNumber(cl);
    cl->edict = EDICT_NUM(playernum);
}

void SV_New_f(client_t * cl, int argc, cstring_t *argv) {
    (void)argc;
    (void)argv;

    if (sv.state == ss_lobby) {
        SV_LobbyWriteSetup(cl);
        Netchan_Transmit(NS_SERVER, &cl->netchan);
        return;
    }
    SV_SendLoadingScreen(cl);
    MSG_WriteByte(&cl->netchan.message, svc_mirror);
    MSG_WriteString(&cl->netchan.message, "configstrings");
    Netchan_Transmit(NS_SERVER, &cl->netchan);
}

static uint32_t SV_ClientIndex(client_t * client) {
    if (!client || client < svs.clients || client >= svs.clients + MAX_CLIENTS) {
        return 0;
    }
    return (uint32_t)(client - svs.clients);
}

static void SV_LobbySayClient_f(client_t * cl, int argc, cstring_t *argv) {
    char text[256];
    size_t used = 0;
    char sender[32];

    if (argc < 2 || !argv) {
        return;
    }
    text[0] = '\0';
    for (int i = 1; i < argc; i++) {
        cstring_t value = argv[i] ? argv[i] : "";
        size_t len = strlen(value);

        if (used && used + 1 < sizeof(text)) {
            text[used++] = ' ';
            text[used] = '\0';
        }
        if (len >= sizeof(text) - used) {
            len = sizeof(text) - used - 1;
        }
        memcpy(text + used, value, len);
        used += len;
        text[used] = '\0';
    }
    snprintf(sender, sizeof(sender), "Player %u", (unsigned)SV_ClientIndex(cl) + 1);
    SV_LobbyBroadcastChatFrom(SV_ClientIndex(cl), sender, text);
}

typedef struct {
    cstring_t name;
    void (*func)(client_t * client, int argc, cstring_t *argv);
} ucmd_t;

ucmd_t ucmds[] = {
    { "new", SV_New_f },
    { "configstrings", SV_Configstrings_f },
    { "baselines", SV_Baselines_f },
    { "playerinfo", SV_PlayerInfo_f },
    { "begin", SV_Begin_f },
    { "lobby_say", SV_LobbySayClient_f },
    { NULL }
};

void SV_ExecuteUserCommand(sizeBuf_t * msg, client_t * client) {
    typedef char cmdarg_t[CMDARG_LEN];
    static cmdarg_t args[MAX_CMDARGS];
    static cstring_t argv[MAX_CMDARGS];
    uint32_t argc = 0;
    cstring_t command = MSG_ReadString2(msg);
    parser_t p = { 0 };
    p.tok = p.token;
    p.str = command;
    for (cstring_t tok = ParserGetToken(&p); tok && argc < MAX_CMDARGS; tok = ParserGetToken(&p)) {
        strlcpy(args[argc], tok, sizeof(args[argc]));
        argv[argc] = args[argc];
        argc++;
    }
    if (argc == 0) {
        return;
    }
    for (ucmd_t *u = ucmds; u->name; u++) {
        if (!strcmp(argv[0], u->name)) {
            u->func(client, (int)argc, argv);
            return;
        }
    }
    /* Map restart can leave a stringcmd queued before playerinfo assigns the edict. */
    if (!client->edict) return;
    ge->ClientCommand(client->edict, argc, argv);
}
