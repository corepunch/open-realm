#include "g_local.h"

#define CLIENTCOMMAND(NAME) void CMD_##NAME(LPEDICT clent, DWORD argc, LPCSTR argv[])

static BOOL G_TargetModeActive(LPGAMECLIENT client) {
    return client && (client->menu.on_entity_selected || client->menu.on_location_selected);
}

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

/* Client commands arrive before G_RunEntities clears the previous snapshot's
 * event, so retain the chosen acknowledgement until that frame begins. */
void G_QueueSelectionSound(LPEDICT ent) {
    if (ent && ent->sound.num_select)
        ent->sound.pending = ent->sound.select[rand() % ent->sound.num_select];
}

static void G_QueueOrderSound(LPEDICT ent) {
    if (ent && ent->sound.num_yes)
        ent->sound.pending = ent->sound.yes[rand() % ent->sound.num_yes];
}

/* select/point are left-click completion paths for targeted commands.  A
 * right-click Smart action cancels that mode instead of being interpreted as
 * a new order by the units that were selected when targeting began. */
static BOOL G_CancelTargetMode(LPEDICT clent) {
    LPGAMECLIENT client = clent ? clent->client : NULL;

    if (!client || (!client->menu.on_entity_selected && !client->menu.on_location_selected))
        return false;
    memset(&client->menu, 0, sizeof(client->menu));
    Get_Commands_f(clent);
    return true;
}

void CMD_CancelCommand(LPEDICT ent) {
    if (!G_CancelBuildPlacement(ent)) {
        Get_Commands_f(ent);
    }
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
        LPEDICT voice = NULL;
        for (DWORD i = 1; i < argc; i++) {
            DWORD number = atoi(argv[i]);
            if (number >= globals.num_edicts)
                continue;
            LPEDICT e = &globals.edicts[number];
            if (e->s.player == client->ps.number && !G_UnitIsBuilding(e->class_id)) {
                hasunits = true;
            }
        }
        for (DWORD i = 1; i < argc; i++) {
            DWORD number = atoi(argv[i]);
            if (number >= globals.num_edicts)
                continue;
            LPEDICT e = &globals.edicts[number];
            if (e->s.player == client->ps.number) {
                if (hasunits && G_UnitIsBuilding(e->class_id))
                    continue;
                if (!cleared) {
                    FOR_SELECTED_UNITS(client, ent) G_DeselectEntity(client, ent);
                    cleared = true;
                }
                G_SelectEntity(client, e);
                if (!voice) voice = e;
            }
        }
        if (cleared) {
            G_QueueSelectionSound(voice);
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

    if (G_CancelBuildPlacement(clent) || G_CancelTargetMode(clent)) {
        return;
    }
    /* WC3 right-click cancels an active targeted command.  Do not also send
     * a Smart order through the still-selected server-side unit set: doing so
     * can make a click intended only to leave target mode retask that group. */
    if (G_TargetModeActive(client)) {
        Get_Commands_f(clent);
        return;
    }
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
        G_QueueOrderSound(G_GetMainSelectedUnit(client));
        Get_Commands_f(clent);
    }
}

CLIENTCOMMAND(SmartPoint) {
    LPGAMECLIENT client = clent->client;
    VECTOR2 loc;

    if (G_CancelBuildPlacement(clent) || G_CancelTargetMode(clent)) {
        return;
    }
    /* A right-click while an ability is waiting for a target is cancellation,
     * not a move order.  In particular this clears Harvest's entity callback
     * before a later unit click can be consumed as a harvest target. */
    if (G_TargetModeActive(client)) {
        Get_Commands_f(clent);
        return;
    }
    if (argc < 3) {
        return;
    }
    loc = (VECTOR2){ atoi(argv[1]), atoi(argv[2]) };
    if (move_selectlocation(clent, &loc))
        G_QueueOrderSound(G_GetMainSelectedUnit(client));
}

CLIENTCOMMAND(Button) {
    LPCSTR classname;
    LPGAMECLIENT client = clent->client;
    ability_t const *ability;

    if (argc < 2) return;
    if (client->no_ui) {
        if (client->menu.cmdbutton) client->menu.cmdbutton(clent, *((DWORD *)argv[1]));
        return;
    }
    classname = argv[1];
    ability = FindAbilityForCommand(classname);
    if (ability && ability->cmd) {
        client->menu.ability_code = *((DWORD const *)classname);
        ability->cmd(clent);
    } else if (client->menu.cmdbutton) {
        client->menu.cmdbutton(clent, *((DWORD *)classname));
    } else {
        LPEDICT ent = G_GetMainSelectedUnit(client);
        DWORD class_id = 0;

        if (!ent || strlen(classname) != 4) return;
        memcpy(&class_id, classname, sizeof(class_id));
        SP_TrainUnit(ent, class_id);
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
    if (!ent || slot < 0 || (DWORD)slot >= G_InventoryCapacity(ent)) {
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
            ability_t const *ability = FindAbilityForCommand(ability_name);
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
    if (!unit || slot < 0 || (DWORD)slot >= G_InventoryCapacity(unit)) {
        return;
    }
    G_DropItem(unit, (DWORD)slot);
}

CLIENTCOMMAND(Cancel) {
    if (G_CancelBuildPlacement(clent)) {
        return;
    }
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
    G_ActivateUnitFood(spawned);

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
    BOOL requires_ui;
} clientCommand_t;

clientCommand_t clientCommands[] = {
    { "give", CMD_Give, false },
    { "god", CMD_God, false },
    { "kill", CMD_Kill, false },
    { "button", CMD_Button, false },
    { "research", CMD_Research, true },
    { "inventory", CMD_Inventory, true },
    { "dropitem", CMD_DropItem, true },
    { "select", CMD_Select, true },
    { "point", CMD_Point, true },
    { "smart", CMD_Smart, true },
    { "smartpoint", CMD_SmartPoint, true },
    { "cancel", CMD_Cancel, false },
    { "quests", CMD_Quests, false },
    { "hidequests", CMD_HideQuests, false },
    { "quest", CMD_Quest, false },
    { "hidegameresult", CMD_HideGameResult, false },
    { "gameresult_restart", CMD_GameResultRestart, false },
    { "gameresult_quit", CMD_GameResultQuit, false },
    { "debugspawn", CMD_DebugSpawn, false },
    { "menu", CMD_Menu, true },
    { NULL }
};

void G_ClientCommand(LPEDICT ent, DWORD argc, LPCSTR argv[]) {
    for (clientCommand_t const *cmd = clientCommands; cmd->name; cmd++) {
        if (!strcmp(cmd->name, argv[0])) {
            if (cmd->requires_ui && ent->client->no_ui) return;
            cmd->func(ent, argc, argv);
            return;
        }
    }
}

void G_ClientSetCameraPosition(LPEDICT ent, LPCVECTOR2 position) {
    VECTOR2 clamped;

    if (ent->client->no_control)
        return;
    clamped = G_ClampCameraPosition(ent->client, position);
    G_ClearCameraTarget(ent->client, "G_ClientSetCameraPosition");
    ent->client->camera.old_state = ent->client->camera.state;
    ent->client->camera.state.position = clamped;
    ent->client->camera.start_time = gi.GetTime();
    ent->client->camera.end_time = ent->client->camera.start_time;
}
