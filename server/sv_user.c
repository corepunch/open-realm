#include "server.h"

static DWORD SV_ConfigStringWireSize(DWORD index) {
    if (index == CS_STATUSBAR) {
        return 1 + 2 + sizeof(*sv.configstrings);
    }
    return 1 + 2 + (DWORD)strlen(ge->GetThemeValue(sv.configstrings[index])) + 1;
}

static DWORD SV_ClientPlayerNumber(LPCLIENT cl) {
    return cl->playernum < MAX_PLAYERS ? cl->playernum : 0;
}

static void SV_FlushSpawnMessage(LPCLIENT cl, LPCSTR phase, DWORD count) {
    if (cl->netchan.message.cursize == 0) {
        return;
    }
    if (cl->netchan.message.overflowed) {
        fprintf(stderr,
                "SV_%s_f: transmitting overflowed batch count=%u bytes=%u max=%u\n",
                phase,
                (unsigned)count,
                (unsigned)cl->netchan.message.cursize,
                (unsigned)cl->netchan.message.maxsize);
    }
    Netchan_Transmit(NS_SERVER, &cl->netchan);
}

void SV_Configstrings_f(LPCLIENT cl, int argc, LPCSTR *argv) {
    DWORD batch_count = 0;

    (void)argc;
    (void)argv;

    FOR_LOOP(i, MAX_CONFIGSTRINGS) {
        if (!*sv.configstrings[i])
            continue;
        if (cl->netchan.message.cursize + SV_ConfigStringWireSize(i) + 16 >= cl->netchan.message.maxsize) {
            SV_FlushSpawnMessage(cl, "Configstrings", batch_count);
            batch_count = 0;
        }
        SV_WriteConfigString(&cl->netchan.message, i);
        batch_count++;
    }
    if (cl->netchan.message.cursize + 16 >= cl->netchan.message.maxsize) {
        SV_FlushSpawnMessage(cl, "Configstrings", batch_count);
    }
    MSG_WriteByte(&cl->netchan.message, svc_mirror);
    MSG_WriteString(&cl->netchan.message, "baselines");
    Netchan_Transmit(NS_SERVER, &cl->netchan);
}

void SV_Baselines_f(LPCLIENT cl, int argc, LPCSTR *argv) {
    entityState_t nullstate;
    DWORD batch_count = 0;

    (void)argc;
    (void)argv;

    memset(&nullstate, 0, sizeof(entityState_t));
    FOR_LOOP(index, ge->num_edicts) {
        edict_t *e = EDICT_NUM(index);
        if (e->svflags & SVF_NOCLIENT)
            continue;
        if (cl->netchan.message.cursize + 512 >= cl->netchan.message.maxsize) {
            SV_FlushSpawnMessage(cl, "Baselines", batch_count);
            batch_count = 0;
        }
        MSG_WriteByte(&cl->netchan.message, svc_spawnbaseline);
        MSG_WriteDeltaEntity(&cl->netchan.message, &nullstate, &e->s, true);
        batch_count++;
    }
    if (cl->netchan.message.cursize + 16 >= cl->netchan.message.maxsize) {
        SV_FlushSpawnMessage(cl, "Baselines", batch_count);
    }
    MSG_WriteByte(&cl->netchan.message, svc_mirror);
    MSG_WriteString(&cl->netchan.message, "playerinfo");
    Netchan_Transmit(NS_SERVER, &cl->netchan);
}

void SV_Begin_f(LPCLIENT cl, int argc, LPCSTR *argv) {
    DWORD playernum;

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
    cl->lastframe = (DWORD)-1;
    ge->ClientBegin(cl->edict);
}

void SV_PlayerInfo_f(LPCLIENT cl, int argc, LPCSTR *argv) {
    DWORD playernum;

    (void)argc;
    (void)argv;

    /* Assign the client's game edict (Quake 2/3 pattern) */
    playernum = SV_ClientPlayerNumber(cl);
    cl->edict = EDICT_NUM(playernum);
}

void SV_New_f(LPCLIENT cl, int argc, LPCSTR *argv) {
    (void)argc;
    (void)argv;

    if (sv.state == ss_lobby) {
        SV_LobbyWriteSetup(cl);
        Netchan_Transmit(NS_SERVER, &cl->netchan);
        return;
    }
    MSG_WriteByte(&cl->netchan.message, svc_mirror);
    MSG_WriteString(&cl->netchan.message, "configstrings");
    Netchan_Transmit(NS_SERVER, &cl->netchan);
}

static DWORD SV_ClientIndex(LPCLIENT client) {
    if (!client || client < svs.clients || client >= svs.clients + MAX_CLIENTS) {
        return 0;
    }
    return (DWORD)(client - svs.clients);
}

static void SV_LobbySayClient_f(LPCLIENT cl, int argc, LPCSTR *argv) {
    char text[256];
    size_t used = 0;
    char sender[32];

    if (argc < 2 || !argv) {
        return;
    }
    text[0] = '\0';
    for (int i = 1; i < argc; i++) {
        LPCSTR value = argv[i] ? argv[i] : "";
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
    LPCSTR name;
    void (*func)(LPCLIENT client, int argc, LPCSTR *argv);
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

void SV_ExecuteUserCommand(LPSIZEBUF msg, LPCLIENT client) {
    typedef char cmdarg_t[CMDARG_LEN];
    static cmdarg_t args[MAX_CMDARGS];
    static LPCSTR argv[MAX_CMDARGS];
    DWORD argc = 0;
    LPCSTR command = MSG_ReadString2(msg);
    parser_t p = { 0 };
    p.tok = p.token;
    p.str = command;
    for (LPCSTR tok = ParserGetToken(&p); tok && argc < MAX_CMDARGS; tok = ParserGetToken(&p)) {
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
    ge->ClientCommand(client->edict, argc, argv);
}
