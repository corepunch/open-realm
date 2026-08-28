#include "g_local.h"

#define CLIENTCOMMAND(NAME) void CMD_##NAME(LPEDICT clent, DWORD argc, LPCSTR argv[])

LPEDICT G_GetMainSelectedUnit(LPGAMECLIENT client) {
    FOR_SELECTED_UNITS(client, ent) {
        return ent;
    }
    return NULL;
}

void G_SelectEntity(LPGAMECLIENT client, LPEDICT ent) {
    ent->selected |= 1 << client->ps.number;
}

void G_DeselectEntity(LPGAMECLIENT client, LPEDICT ent) {
    ent->selected &= ~(1 << client->ps.number);
}

BOOL G_IsEntitySelected(LPGAMECLIENT client, LPEDICT ent) {
    return ent->selected & (1 << client->ps.number);
}

void CMD_CancelCommand(LPEDICT ent) {
    Get_Commands_f(ent);
}

CLIENTCOMMAND(Select) {
    LPGAMECLIENT client = clent->client;
    if (client->menu.on_entity_selected) {
        DWORD number = atoi(argv[1]);
        if (number >= globals.num_edicts)
            return;
        if (client->menu.on_entity_selected(clent, &globals.edicts[number])) {
            Get_Commands_f(clent);
        }
    } else {
        BOOL cleared = false;
        BOOL hasunits = false;
        for (DWORD i = 1; i < argc; i++) {
            DWORD number = atoi(argv[i]);
            if (number >= globals.num_edicts)
                continue;
            LPEDICT e = &globals.edicts[number];
            if (e->s.player == client->ps.number && !UNIT_IS_BUILDING(e->class_id)) {
                hasunits = true;
            }
        }
        for (DWORD i = 1; i < argc; i++) {
            DWORD number = atoi(argv[i]);
            if (number >= globals.num_edicts)
                continue;
            LPEDICT e = &globals.edicts[number];
            if (e->s.player == client->ps.number) {
                if (hasunits && UNIT_IS_BUILDING(e->class_id))
                    continue;
                if (!cleared) {
                    FOR_SELECTED_UNITS(client, ent) G_DeselectEntity(client, ent);
                    cleared = true;
                }
                G_SelectEntity(client, e);
            }
        }
        if (cleared) {
            Get_Portrait_f(clent);
            Get_Commands_f(clent);
        }
    }
}

CLIENTCOMMAND(Point) {
    LPGAMECLIENT client = clent->client;
    if (client->menu.on_location_selected) {
        VECTOR2 loc = { atoi(argv[1]), atoi(argv[2]) };
        if (client->menu.on_location_selected(clent, &loc)) {
            Get_Commands_f(clent);
        }
    }
}

CLIENTCOMMAND(Smart) {
    LPGAMECLIENT client = clent->client;
    BOOL issued = false;
    DWORD number;
    LPEDICT target;

    if (argc < 2) {
        return;
    }
    number = atoi(argv[1]);
    if (number >= globals.num_edicts) {
        return;
    }
    target = &globals.edicts[number];
    FOR_SELECTED_UNITS(client, ent) {
        if (unit_issuetargetorder(ent, "smart", target)) {
            issued = true;
        }
    }
    if (issued) {
        Get_Commands_f(clent);
    }
}

CLIENTCOMMAND(SmartPoint) {
    VECTOR2 loc;

    if (argc < 3) {
        return;
    }
    loc = (VECTOR2){ atoi(argv[1]), atoi(argv[2]) };
    move_selectlocation(clent, &loc);
}

CLIENTCOMMAND(Button) {
    LPCSTR classname = argv[1];
    LPGAMECLIENT client = clent->client;
    LPCSTR code = game.config.abilities ? FS_FindSheetCell(game.config.abilities, classname, "code") : NULL;
    ability_t const *ability = FindAbilityByClassname(code ? code : classname);
    if (ability && ability->cmd) {
        client->menu.ability_code = *((DWORD const *)classname);
        ability->cmd(clent);
    } else if (client->menu.cmdbutton) {
        client->menu.cmdbutton(clent, *((DWORD *)classname));
    } else {
        LPEDICT ent = G_GetMainSelectedUnit(client);
        LPCSTR builds = UNIT_TRAINS(ent->class_id);
        if (!builds)
            return;
        PARSE_LIST(builds, build, parse_segment) {
            if (!strcmp(build, classname)) {
                SP_TrainUnit(ent, *((DWORD *)classname));
                break;
            }
        }
    }
}

CLIENTCOMMAND(Research) {
    LPCSTR classname = argv[1];
    LPGAMECLIENT client = clent->client;
//    ability_t const *ability = FindAbilityByClassname(classname);
//    if (!ability) {
//        gi.error("No such ability %s", classname);
//        return;
//    }
    LPEDICT ent = G_GetMainSelectedUnit(client);
    DWORD abilcode = *(DWORD const *)classname;
    unit_learnability(ent, abilcode);
    Get_Commands_f(clent);
}

static BOOL G_CheatsEnabled(void) {
    return gi.CvarString && atoi(gi.CvarString("sv_cheats", "0")) != 0;
}

static LPEDICT G_GiveItem(LPEDICT unit, DWORD item_code) {
    LPEDICT item = SP_SpawnAtLocation(item_code, unit->s.player, &unit->s.origin2);
    if (!item || !G_PickupItem(unit, item)) {
        if (item) G_RemoveItem(item);
        return NULL;
    }
    return item;
}

CLIENTCOMMAND(Give) {
    LPGAMECLIENT client = clent->client;
    LPEDICT unit = G_GetMainSelectedUnit(client);
    DWORD code;

    if (!G_CheatsEnabled()) {
        fprintf(stderr, "WC3: cheats are disabled; set sv_cheats 1\n");
        return;
    }
    if (!unit || argc < 2) {
        fprintf(stderr, "WC3: cheats: give item <rawcode> [count] | ability <rawcode> | xp <amount>\n");
        return;
    }
    if (argc < 3) {
        fprintf(stderr, "WC3: give requires a target and value\n");
        return;
    }
    if (strcasecmp(argv[1], "xp") && strlen(argv[2]) < 4) {
        fprintf(stderr, "WC3: rawcode must contain four characters\n");
        return;
    }
    if (!strcasecmp(argv[1], "item")) {
        code = *(DWORD const *)argv[2];
        if (!G_GiveItem(unit, code)) {
            fprintf(stderr, "WC3: could not give item %.4s to selected unit\n", argv[2]);
            return;
        }
    } else if (!strcasecmp(argv[1], "ability")) {
        code = *(DWORD const *)argv[2];
        unit_learnability(unit, code);
    } else if (!strcasecmp(argv[1], "xp")) {
        if (!G_UnitIsHero(unit)) {
            fprintf(stderr, "WC3: selected unit is not a hero\n");
            return;
        }
        G_HeroSetXP(unit, unit->hero.xp + (DWORD)strtoul(argv[2], NULL, 10));
    } else {
        fprintf(stderr, "WC3: unsupported give target '%s'\n", argv[1]);
        return;
    }
    Get_Commands_f(clent);
    Get_Portrait_f(clent);
}

CLIENTCOMMAND(God) {
    if (!G_CheatsEnabled()) {
        fprintf(stderr, "WC3: cheats are disabled; set sv_cheats 1\n");
        return;
    }
    clent->invulnerable = !clent->invulnerable;
    fprintf(stderr, "WC3: god %s\n", clent->invulnerable ? "on" : "off");
}

CLIENTCOMMAND(Kill) {
    if (!G_CheatsEnabled()) {
        fprintf(stderr, "WC3: cheats are disabled; set sv_cheats 1\n");
        return;
    }
    clent->health.value = 0;
}

CLIENTCOMMAND(Inventory) {
    LPGAMECLIENT client = clent->client;
    LPEDICT ent;
    LPEDICT item;
    LONG slot;
    LPCSTR abilities;
    BOOL handled = false;

    if (argc < 2) {
        return;
    }

    ent = G_GetMainSelectedUnit(client);
    slot = atoi(argv[1]);
    if (!ent || slot < 0 || slot >= MAX_INVENTORY) {
        return;
    }

    item = ent->inventory[slot];
    if (!item || !item->class_id) {
        return;
    }

    G_PublishEvent(ent, EVENT_PLAYER_UNIT_USE_ITEM);
    G_PublishEvent(ent, EVENT_UNIT_USE_ITEM);

    abilities = FindConfigValue(GetClassName(item->class_id), "abilList");
    if (abilities && *abilities) {
        PARSE_LIST(abilities, ability_name, parse_segment) {
            LPCSTR code = game.config.abilities ? FS_FindSheetCell(game.config.abilities, ability_name, "code") : NULL;
            ability_t const *ability = FindAbilityByClassname(code ? code : ability_name);
            if (ability && ability->cmd) {
                client->menu.ability_code = *((DWORD const *)ability_name);
                ability->cmd(clent);
                handled = true;
                break;
            }
        }
    }

    Get_Portrait_f(clent);
    if (!handled) {
        Get_Commands_f(clent);
    }
}

CLIENTCOMMAND(DropItem) {
    LPEDICT unit;
    LONG slot;

    if (!clent || !clent->client || argc < 2) {
        return;
    }
    unit = G_GetMainSelectedUnit(clent->client);
    slot = atoi(argv[1]);
    if (!unit || slot < 0 || slot >= MAX_INVENTORY) {
        return;
    }
    G_DropItem(unit, (DWORD)slot);
}

CLIENTCOMMAND(Cancel) {
    fprintf(stderr,
            "Client cancel command: player=%u edict=%u time=%u\n",
            clent && clent->client ? (unsigned)clent->client->ps.number : 999u,
            clent ? (unsigned)clent->s.number : 999u,
            (unsigned)gi.GetTime());
    G_PublishEvent(clent, EVENT_PLAYER_END_CINEMATIC);
    if (level.mapinfo) {
        FOR_LOOP(i, game.max_clients) {
            LPEDICT ent = G_GetPlayerEntityByNumber(i);
            if (ent && ent != clent &&
                level.mapinfo->players[i].playerType == kPlayerTypeHuman)
            {
                fprintf(stderr,
                        "Client cancel command: also publishing for human player=%u edict=%u\n",
                        (unsigned)i,
                        (unsigned)ent->s.number);
                G_PublishEvent(ent, EVENT_PLAYER_END_CINEMATIC);
            }
        }
    }
}

void UI_ShowQuest(LPEDICT ent, LPCQUEST quest);

CLIENTCOMMAND(Quests) {
    UI_ShowQuests(clent);
}

CLIENTCOMMAND(HideQuests) {
    UI_HideQuests(clent);
}

CLIENTCOMMAND(HideGameResult) {
    UI_HideGameResult(clent);
}

/* TODO: restart / quit require engine-level session teardown not yet plumbed. */
CLIENTCOMMAND(GameResultRestart) {
    (void)clent; (void)argc; (void)argv;
}

CLIENTCOMMAND(GameResultQuit) {
    (void)clent; (void)argc; (void)argv;
}

/* CMD_Menu: Stub for legacy menu commands.
 * Menu rendering is now handled by client-side UI library (Phase 4).
 * Server-side menu commands are deprecated. */
CLIENTCOMMAND(Menu) {
    (void)clent;
    (void)argc;
    (void)argv;
    /* Menu commands now handled by client UI library */
}

CLIENTCOMMAND(Quest) {
    DWORD index = atoi(argv[1]);
    FOR_EACH_LIST(QUEST, q, level.quests) {
        if (index == 0) {
            UI_ShowQuest(clent, q);
            break;
        } else {
            index--;
        }
    }
}

static BOOL G_DebugIsNumber(LPCSTR text) {
    if (!text || !*text) {
        return false;
    }
    if (*text == '-' || *text == '+') {
        text++;
    }
    if (!*text) {
        return false;
    }
    while (*text) {
        if (!isdigit((unsigned char)*text)) {
            return false;
        }
        text++;
    }
    return true;
}

CLIENTCOMMAND(DebugSpawn) {
    LPGAMECLIENT client = clent->client;
    DWORD class_id;
    VECTOR2 location;
    LPEDICT spawned;
    DWORD first_ability = 2;

    if (argc < 2 || strlen(argv[1]) < 4) {
        fprintf(stderr, "usage: debugspawn <unitid> [x y] [ability ...]\n");
        return;
    }

    class_id = *((DWORD const *)argv[1]);
    location = client->ps.origin;
    if (argc >= 4 && G_DebugIsNumber(argv[2]) && G_DebugIsNumber(argv[3])) {
        location.x = atoi(argv[2]);
        location.y = atoi(argv[3]);
        first_ability = 4;
    } else {
        LPEDICT selected = G_GetMainSelectedUnit(client);
        if (selected) {
            location = selected->s.origin2;
            location.x += selected->collision + 96.0f;
        }
    }

    spawned = SP_SpawnAtLocation(class_id, client->ps.number, &location);
    if (!spawned) {
        return;
    }

    for (DWORD i = first_ability; i < argc; i++) {
        if (strlen(argv[i]) >= 4) {
            unit_learnability(spawned, *((DWORD const *)argv[i]));
        }
    }

    FOR_SELECTED_UNITS(client, ent) {
        G_DeselectEntity(client, ent);
    }
    G_SelectEntity(client, spawned);
    Get_Portrait_f(clent);
    Get_Commands_f(clent);
}

typedef struct {
    LPCSTR name;
    void (*func)(LPEDICT ent, DWORD argc, LPCSTR argv[]);
} clientCommand_t;

clientCommand_t clientCommands[] = {
    { "give", CMD_Give },
    { "god", CMD_God },
    { "kill", CMD_Kill },
    { "button", CMD_Button },
    { "research", CMD_Research },
    { "inventory", CMD_Inventory },
    { "dropitem", CMD_DropItem },
    { "select", CMD_Select },
    { "point", CMD_Point },
    { "smart", CMD_Smart },
    { "smartpoint", CMD_SmartPoint },
    { "cancel", CMD_Cancel },
    { "quests", CMD_Quests },
    { "hidequests", CMD_HideQuests },
    { "quest", CMD_Quest },
    { "hidegameresult", CMD_HideGameResult },
    { "gameresult_restart", CMD_GameResultRestart },
    { "gameresult_quit", CMD_GameResultQuit },
    { "debugspawn", CMD_DebugSpawn },
    { "menu", CMD_Menu },
    { NULL }
};

void G_ClientCommand(LPEDICT ent, DWORD argc, LPCSTR argv[]) {
    for (clientCommand_t const *cmd = clientCommands; cmd->name; cmd++) {
        if (!strcmp(cmd->name, argv[0])) {
            cmd->func(ent, argc, argv);
            return;
        }
    }
}

void G_ClientSetCameraPosition(LPEDICT ent, LPCVECTOR2 position) {
    if (ent->client->no_control)
        return;
    VECTOR2 clamped = *position;
    if (level.mapinfo) {
        FLOAT const *b = level.mapinfo->cameraBounds.bounds;
        FLOAT min_x = MIN(MIN(b[0], b[2]), MIN(b[4], b[6]));
        FLOAT max_x = MAX(MAX(b[0], b[2]), MAX(b[4], b[6]));
        FLOAT min_y = MIN(MIN(b[1], b[3]), MIN(b[5], b[7]));
        FLOAT max_y = MAX(MAX(b[1], b[3]), MAX(b[5], b[7]));
        if (max_x > min_x && max_y > min_y) {
            clamped.x = MAX(min_x, MIN(max_x, clamped.x));
            clamped.y = MAX(min_y, MIN(max_y, clamped.y));
        }
    }
    G_ClearCameraTarget(ent->client, "G_ClientSetCameraPosition");
    ent->client->camera.old_state = ent->client->camera.state;
    ent->client->camera.state.position = clamped;
    ent->client->camera.start_time = gi.GetTime();
    ent->client->camera.end_time = ent->client->camera.start_time;
}
