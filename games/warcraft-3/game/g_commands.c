#include <stdarg.h>

#include "g_local.h"

#define CLIENTCOMMAND(NAME) void CMD_##NAME(LPEDICT clent, DWORD argc, LPCSTR argv[])
#define WC3_SELECTION_LIMIT 12

/* Focus is presentation/input state within an existing selection, not a saved
 * gameplay relationship. Keep it out of GAMECLIENT so save compatibility does
 * not depend on which multiselect subgroup happened to own the HUD. */
static DWORD selection_focus[MAX_CLIENTS];
static BOOL G_DebugIsNumber(LPCSTR text);

static DWORD *G_SelectionFocusSlot(LPGAMECLIENT client) {
    LONG index;

    if (!client || !game.clients) return NULL;
    index = (LONG)(client - game.clients);
    if (index < 0 || index >= MAX_CLIENTS) return NULL;
    return &selection_focus[index];
}

static BOOL G_TargetModeActive(LPGAMECLIENT client) {
    return client && (client->menu.on_entity_selected || client->menu.on_location_selected);
}

static BOOL G_CommandQueueRequested(DWORD argc, LPCSTR argv[], DWORD first_optional) {
    for (DWORD i = first_optional; i < argc; i++) {
        if (!strcmp(argv[i], "queue")) return true;
    }
    return false;
}

static BOOL G_ParseEntityNumber(LPCSTR text, DWORD *number) {
    char *end = NULL;
    unsigned long value;

    if (!text || !*text || !number) return false;
    value = strtoul(text, &end, 10);
    if (!end || *end || value >= globals.num_edicts) return false;
    *number = (DWORD)value;
    return true;
}

LPEDICT G_GetMainSelectedUnit(LPGAMECLIENT client) {
    DWORD *focus = G_SelectionFocusSlot(client);

    if (focus && *focus > 0 && *focus < globals.num_edicts) {
        LPEDICT ent = &globals.edicts[*focus];
        if (G_IsEntitySelected(client, ent)) return ent;
    }
    FOR_SELECTED_UNITS(client, ent) {
        if (focus) *focus = ent->s.number;
        return ent;
    }
    if (focus) *focus = 0;
    return NULL;
}


void G_SyncClientSelection(LPGAMECLIENT client) {
    LPEDICT clent;
    DWORD selected[WC3_SELECTION_LIMIT];
    DWORD count = 0;

    if (!client) return;
    FOR_SELECTED_UNITS(client, ent) {
        if (count >= WC3_SELECTION_LIMIT) break;
        selected[count++] = ent->s.number;
    }

    if (!client->connected) {
        G_InvalidateCommands(client);
        return;
    }
    clent = G_GetPlayerEntityByNumber(client->ps.number);
    if (!clent || clent->client != client) {
        G_InvalidateCommands(client);
        return;
    }

    gi.Write(PF_BYTE, &(LONG){svc_set_selection});
    gi.Write(PF_BYTE, &(LONG){count});
    FOR_LOOP(i, count) gi.Write(PF_LONG, &(LONG){selected[i]});
    gi.unicast(clent);

    Get_Portrait_f(clent);
    Get_Commands_f(clent);
}

BOOL G_FocusSelectedUnit(LPGAMECLIENT client, LPEDICT ent) {
    DWORD *focus = G_SelectionFocusSlot(client);

    if (!focus || !G_IsEntitySelected(client, ent)) return false;
    *focus = ent->s.number;
    return true;
}

void G_ResetSelectionFocus(LPGAMECLIENT client) {
    DWORD *focus = G_SelectionFocusSlot(client);
    if (focus) *focus = 0;
}

LPEDICT G_GetMainControllableUnit(LPGAMECLIENT client) {
    LPEDICT main = G_GetMainSelectedUnit(client);

    if (main && G_UnitCanControl(client, main)) return main;
    FOR_CONTROLLABLE_SELECTED_UNITS(client, ent) {
        return ent;
    }
    return NULL;
}

void G_SelectEntity(LPGAMECLIENT client, LPEDICT ent) {
    BOOL had_selection = false;

    /* Corpses remain networked while their death/decay presentation runs, but
     * they are no longer valid gameplay selection targets. */
    if (!client || !ent || !ent->inuse) return;
    if (M_IsDead(ent) || (ent->s.flags & EF_NOT_SELECTABLE)) return;
    FOR_SELECTED_UNITS(client, selected) {
        (void)selected;
        had_selection = true;
        break;
    }
    ent->selected |= 1 << client->ps.number;
    if (!had_selection) G_FocusSelectedUnit(client, ent);
}

void G_DeselectEntity(LPGAMECLIENT client, LPEDICT ent) {
    DWORD *focus;

    if (!client || !ent) return;
    focus = G_SelectionFocusSlot(client);
    if (focus && *focus == ent->s.number) *focus = 0;
    ent->selected &= ~(1 << client->ps.number);
}

BOOL G_IsEntitySelected(LPGAMECLIENT client, LPEDICT ent) {
    return client && ent && ent->inuse && !M_IsDead(ent) &&
        !(ent->s.flags & EF_NOT_SELECTABLE) && !(ent->s.renderfx & RF_HIDDEN) &&
        (ent->selected & (1 << client->ps.number));
}

selectionRelation_t G_SelectionRelation(DWORD viewer, LPCEDICT ent) {
    DWORD owner;
    DWORD alliances;

    if (!ent) {
        return SELECT_RELATION_ENEMY;
    }
    owner = ent->s.player;
    if (owner == viewer) {
        return SELECT_RELATION_FRIEND;
    }
    if (viewer >= MAX_PLAYERS || owner >= MAX_PLAYERS) {
        return SELECT_RELATION_ENEMY;
    }
    alliances = level.alliances[viewer][owner];
    if (!G_PlayerTreatsPlayerAsAlly(viewer, owner)) {
        return SELECT_RELATION_ENEMY;
    }
    if (alliances & (1 << ALLIANCE_SHARED_CONTROL)) {
        return SELECT_RELATION_FRIEND;
    }
    return SELECT_RELATION_NEUTRAL;
}

BOOL G_UnitCanBeSelected(LPGAMECLIENT client, LPCEDICT ent) {
    if (!client || !ent || !ent->inuse || !(ent->svflags & SVF_MONSTER)) {
        return false;
    }
    if ((ent->svflags & SVF_DEADMONSTER) || ent->health.value <= 0.0f ||
        (ent->s.flags & EF_NOT_SELECTABLE) || (ent->s.renderfx & RF_HIDDEN)) {
        return false;
    }
    return G_FowPlayerCanHoverEntity(client->ps.number, ent);
}

BOOL G_UnitCanControl(LPGAMECLIENT client, LPCEDICT ent) {
    DWORD owner;
    DWORD alliances;

    /* Control is an authority relationship, not a visibility/selectability
     * test.  Callers that issue player orders already operate on an active
     * selected unit via G_IsEntitySelected(), which filters dead, hidden, and
     * unselectable entities.  Keeping these decisions separate prevents fog
     * or presentation state from revoking ownership/shared-control rights. */
    if (!client || !ent || !ent->inuse) {
        return false;
    }
    owner = ent->s.player;
    if (owner == client->ps.number) {
        return true;
    }
    if (owner >= MAX_PLAYERS || client->ps.number >= MAX_PLAYERS) {
        return false;
    }
    alliances = level.alliances[client->ps.number][owner];
    return G_PlayerTreatsPlayerAsAlly(client->ps.number, owner) &&
           (alliances & (1 << ALLIANCE_SHARED_CONTROL)) != 0;
}

void G_UpdateClientSelections(void) {
    FOR_LOOP(i, game.max_clients) {
        LPGAMECLIENT client = game.clients + i;
        BOOL changed = false;
        DWORD bit = 1 << client->ps.number;

        /* Inspect the raw bit here rather than FOR_SELECTED_UNITS.  The latter
         * deliberately hides dead/unselectable entities, while this pass must
         * clear stale selection bits after visibility/selectability changes. */
        FILTER_EDICTS(ent, ent->selected & bit) {
            if (!G_UnitCanBeSelected(client, ent)) {
                G_DeselectEntity(client, ent);
                changed = true;
            }
        }
        if (!changed) {
            continue;
        }
        G_SyncClientSelection(client);
    }
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

static void G_PrepareUnitShortcut(LPEDICT clent) {
    if (!clent || !clent->client) return;
    G_CancelBuildPlacement(clent);
    G_CancelTargetMode(clent);
}

CLIENTCOMMAND(HeroButton) {
    if (argc < 2) return;
    G_PrepareUnitShortcut(clent);
    G_ActivateHeroButton(clent, (DWORD)atoi(argv[1]));
}

CLIENTCOMMAND(HeroKey) {
    if (argc < 2) return;
    G_PrepareUnitShortcut(clent);
    G_ActivateHeroKey(clent, (DWORD)atoi(argv[1]));
}

CLIENTCOMMAND(IdleWorker) {
    DWORD hinted_number = argc >= 2 ? (DWORD)atoi(argv[1]) : 0;
    G_PrepareUnitShortcut(clent);
    G_ActivateIdleWorkerShortcut(clent, hinted_number);
}

void CMD_CancelCommand(LPEDICT ent) {
    LPEDICT producer;
    if (ent && ent->client && (producer = G_GetMainSelectedUnit(ent->client)) &&
        G_UnitCanControl(ent->client, producer)) {
        /* Spawned construction is cancelled by the structure itself. Keep it
         * ahead of queue cancellation so CmdCancelBuild cannot fall through to
         * unrelated producer state. */
        if (producer->construction.active && G_CancelStructureConstruction(producer)) {
            if (ent->client->connected) {
                G_RefreshResourceBar(ent);
                Get_Portrait_f(ent);
                Get_Commands_f(ent);
            }
            return;
        }
        if (producer->build && producer->build->revival.reviving &&
            G_CancelHeroRevive(producer, producer->build)) {
            Get_Commands_f(ent);
            return;
        }
    }
    if (!G_CancelBuildPlacement(ent)) {
        Get_Commands_f(ent);
    }
}

CLIENTCOMMAND(Select) {
    LPGAMECLIENT client = clent->client;
    if (client->menu.on_entity_selected) {
        DWORD number;
        BOOL const queued = client->menu.supports_order_queue &&
                            G_CommandQueueRequested(argc, argv, 2);
        BOOL accepted;

        if (argc < 2 || !G_ParseEntityNumber(argv[1], &number)) return;
        client->menu.order_queued = queued;
        accepted = client->menu.on_entity_selected(clent, &globals.edicts[number]);
        client->menu.order_queued = false;
        /* Warsmash keeps a target command armed while Shift is held so the
         * player can click several waypoints/targets without reopening it. */
        if (accepted && !queued) Get_Commands_f(clent);
    } else {
        BOOL cleared = false;
        BOOL hasunits = false;
        LPEDICT voice = NULL;
        DWORD selected_count = 0;
        for (DWORD i = 1; i < argc; i++) {
            DWORD number;
            if (!G_ParseEntityNumber(argv[i], &number)) continue;
            LPEDICT e = &globals.edicts[number];
            if (G_UnitCanBeSelected(client, e) && G_UnitCanControl(client, e) &&
                !G_UnitIsBuilding(e->class_id)) {
                hasunits = true;
            }
        }
        for (DWORD i = 1; i < argc; i++) {
            DWORD number;
            if (!G_ParseEntityNumber(argv[i], &number)) continue;
            LPEDICT e = &globals.edicts[number];
            if (G_UnitCanBeSelected(client, e)) {
                if (hasunits && (!G_UnitCanControl(client, e) || G_UnitIsBuilding(e->class_id)))
                    continue;
                if (!cleared) {
                    FOR_SELECTED_UNITS(client, ent) G_DeselectEntity(client, ent);
                    cleared = true;
                }
                if (G_IsEntitySelected(client, e)) {
                    continue;
                }
                if (selected_count >= WC3_SELECTION_LIMIT) {
                    break;
                }
                G_SelectEntity(client, e);
                selected_count++;
                if (!voice) voice = e;
            }
        }
        if (cleared) {
            if (G_UnitCanControl(client, voice)) {
                G_QueueSelectionSound(voice);
            } else if (voice && voice->s.player != PLAYER_NEUTRAL_PASSIVE) {
                /* Ordinary foreign units use interface feedback rather than
                 * speaking their owner's selection acknowledgement. Neutral
                 * Passive critter response rules remain a separate gap. */
                G_PlayUISoundForPlayer(clent, "InterfaceClick");
            }
            /* Selection is authoritative game state. Mirror the accepted,
             * server-filtered membership back to the client cache as well as
             * rebuilding the HUD so the client cannot retain current-selection entries
             * that the server rejected. */
            G_SyncClientSelection(client);
        }
    }
}

void G_SendPointConfirmation(LPEDICT clent, LPCVECTOR2 point, BOOL attack) {
    if (!clent || !clent->client || !point) return;
    gi.Write(PF_BYTE, &(LONG){ svc_temp_entity });
    gi.Write(PF_BYTE, &(LONG){ attack ? TE_ATTACK_CONFIRMATION : TE_MOVE_CONFIRMATION });
    gi.Write(PF_POSITION, &(VECTOR3){ point->x, point->y, 0 });
    gi.unicast(clent);
}

CLIENTCOMMAND(Focus) {
    LPGAMECLIENT client = clent ? clent->client : NULL;
    DWORD number;
    LPEDICT target;

    if (!client || argc < 2) return;
    number = (DWORD)atoi(argv[1]);
    if (number >= globals.num_edicts) return;
    target = &globals.edicts[number];
    if (!G_IsEntitySelected(client, target)) return;

    /* Warsmash treats a multiselect portrait as the clicked unit while an
     * entity-target command is active. Do not silently change subgroup focus
     * instead of completing/cancelling that target interaction. */
    if (G_TargetModeActive(client)) {
        if (client->menu.on_entity_selected &&
            client->menu.on_entity_selected(clent, target)) {
            Get_Commands_f(clent);
        }
        return;
    }
    if (!G_FocusSelectedUnit(client, target)) return;

    /* Selection membership is unchanged. Rebuild the full focused-selection
     * presentation so the multiselect subgroup highlight, inventory, portrait
     * and command card all move together. */
    Get_Portrait_f(clent);
    Get_Commands_f(clent);
}

CLIENTCOMMAND(Point) {
    LPGAMECLIENT client = clent->client;
    if (argc < 3) return;
    if (client->menu.on_location_selected) {
        BOOL const queued = client->menu.supports_order_queue &&
                            G_CommandQueueRequested(argc, argv, 3);
        VECTOR2 loc = { atoi(argv[1]), atoi(argv[2]) };
        BOOL accepted;

        client->menu.order_queued = queued;
        accepted = client->menu.on_location_selected(clent, &loc);
        client->menu.order_queued = false;
        if (accepted && !queued) Get_Commands_f(clent);
    }
}

CLIENTCOMMAND(Smart) {
    LPGAMECLIENT client = clent->client;
    BOOL issued = false;
    BOOL rallied = false;
    BOOL queued;
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
    if (argc < 2 || !G_ParseEntityNumber(argv[1], &number)) {
        return;
    }
    target = &globals.edicts[number];
    queued = G_CommandQueueRequested(argc, argv, 2);
    FOR_CONTROLLABLE_SELECTED_UNITS(client, ent) {
        if (G_IssueUnitTargetOrder(ent, "smart", target, queued, client->ps.number)) {
            if (G_UnitHasRally(ent)) rallied = true;
            issued = true;
        }
    }
    if (issued) {
        G_QueueOrderSound(G_GetMainControllableUnit(client));
        if (rallied) G_PlayUISoundForPlayer(clent, "RallyPointPlace");
        Get_Commands_f(clent);
    }
}

CLIENTCOMMAND(SmartPoint) {
    LPGAMECLIENT client = clent->client;
    VECTOR2 loc;
    BOOL rally = false;
    BOOL non_rally = false;
    BOOL issued = false;
    BOOL queued;

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
    queued = G_CommandQueueRequested(argc, argv, 3);
    FOR_CONTROLLABLE_SELECTED_UNITS(client, ent) {
        if (G_UnitHasRally(ent)) {
            if (G_IssueUnitPointOrder(ent, "smart", &loc, queued, client->ps.number, 0.0f))
                rally = true;
        } else {
            non_rally = true;
        }
    }
    /* Normal unit SmartPoint retains the existing formation-aware move path.
     * Selection rules normally keep production structures separate from mobile
     * units, so rally-capable selections do not enter this path. */
    if (non_rally) {
        BOOL const old_queued = client->menu.order_queued;
        client->menu.order_queued = queued;
        if (move_selectlocation(clent, &loc)) issued = true;
        client->menu.order_queued = old_queued;
    }
    if (rally || issued) {
        G_QueueOrderSound(G_GetMainControllableUnit(client));
    }
    if (rally) {
        G_SendPointConfirmation(clent, &loc, false);
        G_PlayUISoundForPlayer(clent, "RallyPointPlace");
    }
}

CLIENTCOMMAND(Button) {
    char ability_name[5] = {0};
    LPCSTR classname;
    LPGAMECLIENT client = clent->client;
    ability_t const *ability;
    LPEDICT producer;
    BOOL ability_off = false;

    if (argc < 2) return;
    producer = G_GetMainSelectedUnit(client);
    if (!G_UnitCanControl(client, producer)) return;
    classname = argv[1];
    if (!strncmp(classname, "revive:", 7)) {
        char *end = NULL;
        unsigned long const number = strtoul(classname + 7, &end, 10);
        if (!end || *end || number >= globals.num_edicts) return;
        G_QueueHeroRevive(producer, &globals.edicts[number]);
        return;
    }
    if (strlen(classname) == 8 && !strcmp(classname + 4, ":off")) {
        memcpy(ability_name, classname, 4);
        classname = ability_name;
        ability_off = true;
    }
    ability = FindAbilityForCommand(classname);
    if (ability && ability->cmd) {
        client->menu.ability_code = *((DWORD const *)classname);
        client->menu.ability_off = ability_off;
        ability->cmd(clent);
        client->menu.ability_off = false;
    } else if (client->menu.cmdbutton) {
        client->menu.cmdbutton(clent, *((DWORD *)classname));
    } else {
        DWORD class_id = 0;

        if (strlen(classname) != 4) return;
        memcpy(&class_id, classname, sizeof(class_id));
        SP_TrainUnit(producer, class_id);
    }
}

CLIENTCOMMAND(Autocast) {
    LPGAMECLIENT client;
    LPEDICT main;
    ability_t const *ability;
    LPCSTR classname;
    BOOL enabled;
    BOOL changed = false;

    if (!clent || !clent->client || argc < 2) return;
    client = clent->client;
    classname = argv[1];
    if (!classname || strlen(classname) != 4) return;
    main = G_GetMainSelectedUnit(client);
    ability = FindAbilityForCommand(classname);
    if (!G_UnitCanControl(client, main) || !G_ActorHasSkill(main, classname) ||
        !ability || !ability->autocast_set || !ability->autocast_is_on) {
#ifdef WC3_DEBUG_AUTOCAST
        if (G_AutocastDebugLevel() >= 1) {
            fprintf(stderr,
                    "WC3_AUTOCAST command_rejected unit=%ld code=%s control=%d has_skill=%d ability=%p set=%d state=%d\n",
                    main && g_edicts ? (long)(main - g_edicts) : -1L,
                    classname,
                    G_UnitCanControl(client, main) ? 1 : 0,
                    main && G_ActorHasSkill(main, classname) ? 1 : 0,
                    (void *)ability,
                    ability && ability->autocast_set ? 1 : 0,
                    ability && ability->autocast_is_on ? 1 : 0);
        }
#endif
        return;
    }

    enabled = !G_UnitAutocastIsOn(main, ability);
    FOR_CONTROLLABLE_SELECTED_UNITS(client, ent) {
        if (!G_ActorHasSkill(ent, classname)) continue;
        if (G_SetUnitAutocast(ent, ability, enabled)) {
            changed = true;
            /* The toggle itself must not interrupt active work or movement, but
             * an already-idle worker should respond immediately rather than
             * waiting for the next staggered 300 ms acquisition slot. */
            if (enabled && G_UnitIsIdleWorker(ent)) {
#ifdef WC3_DEBUG_AUTOCAST
                BOOL const acquired = G_TryUnitAutocast(ent);
                if (G_AutocastDebugLevel() >= 1) {
                    fprintf(stderr,
                            "WC3_AUTOCAST toggle_acquire unit=%ld code=%s acquired=%d\n",
                            g_edicts ? (long)(ent - g_edicts) : -1L,
                            classname, acquired ? 1 : 0);
                }
#else
                G_TryUnitAutocast(ent);
#endif
            }
        }
    }
    if (!changed) return;

    Get_Commands_f(clent);
    G_PlayUISoundForPlayer(clent, "AutoCastButtonClick");
}

CLIENTCOMMAND(Research) {
    LPCSTR classname = argc >= 2 ? argv[1] : NULL;
    LPGAMECLIENT client = clent->client;
    LPEDICT ent = G_GetMainSelectedUnit(client);
    DWORD abilcode = 0;

    if (!G_UnitCanControl(client, ent) || !classname || strlen(classname) != 4) {
        return;
    }
    memcpy(&abilcode, classname, sizeof(abilcode));
    if (G_ProducerCanResearch(ent, abilcode)) {
        G_QueueResearch(ent, abilcode);
    } else {
        G_HeroLearnSkill(ent, abilcode);
    }
    Get_Commands_f(clent);
}

static BOOL G_CheatsEnabled(void) {
    return atoi(gi.CvarString("sv_cheats", "0")) != 0;
}

static LPEDICT G_GiveItem(LPEDICT unit, DWORD item_code) {
    LPEDICT item = SP_SpawnAtLocation(item_code, unit->s.player, &unit->s.origin2);
    if (!item || !G_PickupItem(unit, item)) {
        if (item) G_RemoveItem(item);
        return NULL;
    }
    return item;
}

static BOOL G_ParseGiveResourceAmount(LPCSTR text, DWORD *amount) {
    unsigned long value;

    if (!amount || !G_DebugIsNumber(text) || text[0] == '-') return false;
    value = strtoul(text, NULL, 10);
    *amount = (DWORD)MIN(value, (unsigned long)USHRT_MAX);
    return true;
}

static void G_AddGiveResource(LPGAMECLIENT client, DWORD state, DWORD amount) {
    DWORD value;

    if (!client || state >= MAX_STATS) return;
    value = client->ps.stats[state];
    client->ps.stats[state] = (USHORT)MIN(value + amount, (DWORD)USHRT_MAX);
}

static BOOL G_GivePlayerResources(LPGAMECLIENT client, LPCSTR target, LPCSTR value) {
    DWORD amount;
    BOOL const give_gold = !strcasecmp(target, "gold") || !strcasecmp(target, "res");
    BOOL const give_lumber = !strcasecmp(target, "lumber") || !strcasecmp(target, "res");

    if (!give_gold && !give_lumber) return false;
    if (!G_ParseGiveResourceAmount(value, &amount)) {
        fprintf(stderr, "WC3: resource amount must be a non-negative integer\n");
        return true;
    }
    if (give_gold) G_AddGiveResource(client, PLAYERSTATE_RESOURCE_GOLD, amount);
    if (give_lumber) G_AddGiveResource(client, PLAYERSTATE_RESOURCE_LUMBER, amount);
    G_InvalidateCommands(client);
    return true;
}

CLIENTCOMMAND(Give) {
    LPGAMECLIENT client = clent->client;
    LPEDICT unit;
    DWORD code;

    if (!G_CheatsEnabled()) {
        fprintf(stderr, "WC3: cheats are disabled; set sv_cheats 1\n");
        return;
    }
    if (argc < 2) {
        fprintf(stderr,
            "WC3: cheats: give gold <amount> | lumber <amount> | res <amount> | "
            "item <rawcode> [count] | ability <rawcode> | xp <amount>\n");
        return;
    }
    if (argc < 3) {
        fprintf(stderr, "WC3: give requires a target and value\n");
        return;
    }
    if (G_GivePlayerResources(client, argv[1], argv[2])) return;

    unit = G_GetMainSelectedUnit(client);
    if (!unit) {
        fprintf(stderr, "WC3: give %s requires a selected unit\n", argv[1]);
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

static void G_CheatGameResult(LPEDICT clent, DWORD game_result) {
    if (!G_CheatsEnabled()) {
        fprintf(stderr, "WC3: cheats are disabled; set sv_cheats 1\n");
        return;
    }
    if (!clent || !clent->client) return;
    G_RemovePlayerWithResult(clent->client->ps.number, game_result);
}

CLIENTCOMMAND(Win) {
    (void)argc; (void)argv;
    G_CheatGameResult(clent, 0);
}

CLIENTCOMMAND(Lose) {
    (void)argc; (void)argv;
    G_CheatGameResult(clent, 1);
}

static FLOAT G_CheatTimeOfDayTarget(BOOL daytime) {
    FLOAT const day_hours = game.constants.gameDayHours;
    FLOAT const dawn = game.constants.dawnTimeGameHours;
    FLOAT const dusk = game.constants.duskTimeGameHours;
    FLOAT target;

    /* Normal Warcraft data is Dawn=6, Dusk=18, DayHours=24, yielding
     * 12:00 for day and 00:00 for night.  Use authored thresholds so custom
     * map Misc data still lands well inside the requested phase. */
    if (day_hours <= 0.0f || dawn < 0.0f || dusk <= dawn || dusk > day_hours)
        return daytime ? day_hours * 0.5f : 0.0f;

    if (daytime)
        return dawn + (dusk - dawn) * 0.5f;

    target = dusk + ((day_hours - dusk) + dawn) * 0.5f;
    if (target >= day_hours) target -= day_hours;
    return target;
}

static void G_CheatSetTimeOfDay(BOOL daytime) {
    if (!G_CheatsEnabled()) {
        fprintf(stderr, "WC3: cheats are disabled; set sv_cheats 1\n");
        return;
    }
    G_SetTimeOfDay(G_CheatTimeOfDayTarget(daytime));
}

CLIENTCOMMAND(Day) {
    (void)clent; (void)argc; (void)argv;
    G_CheatSetTimeOfDay(true);
}

CLIENTCOMMAND(Night) {
    (void)clent; (void)argc; (void)argv;
    G_CheatSetTimeOfDay(false);
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
    if (!G_UnitCanControl(client, ent) || slot < 0 || (DWORD)slot >= G_InventoryCapacity(ent)) {
        return;
    }

    item = ent->inventory[slot];
    if (!item || !item->class_id) {
        return;
    }

    /* Item ability lists are authored in ItemData.slk. Resolve through the
     * typed row first; FindConfigValue() is only a compatibility fallback in
     * G_ItemAbilityList() because it searches TXT/INI tables rather than the
     * ItemData SLK. */
    abilities = G_ItemAbilityList(item);
    if (abilities && *abilities) {
        PARSE_LIST(abilities, ability_name, parse_segment) {
            ability_t const *ability = FindAbilityForCommand(ability_name);
            BOOL succeeded = false;

            if (!ability) continue;
            client->menu.ability_code = *((DWORD const *)ability_name);
            if (ability->item_use) {
                succeeded = ability->item_use(clent);
                handled = true;
            } else if (ability->cmd) {
                /* Preserve existing support for item-authored command abilities
                 * that enter an asynchronous targeting mode. Their eventual
                 * success is not known here, so charge consumption remains the
                 * responsibility of a future targeted-item completion path. */
                ability->cmd(clent);
                handled = true;
            }

            if (succeeded) {
                G_PublishEvent(ent, EVENT_PLAYER_UNIT_USE_ITEM);
                G_PublishEvent(ent, EVENT_UNIT_USE_ITEM);
                G_ConsumeItemCharge(item);
            }
            if (handled) break;
        }
    }

    /* HUD serialization is only valid after ClientBegin; disconnected slots retain authoritative state only. */
    if (client->connected) {
        Get_Portrait_f(clent);
        if (!handled) Get_Commands_f(clent);
    } else if (!handled) {
        G_InvalidateCommands(client);
    }
}

CLIENTCOMMAND(CancelTrain) {
    LPGAMECLIENT client;
    LPEDICT producer;
    char *end = NULL;
    unsigned long parsed;
    DWORD index;

    if (!clent || !clent->client || argc < 2 || !argv[1] || !*argv[1]) return;
    parsed = strtoul(argv[1], &end, 10);
    if (!end || *end || parsed > UINT_MAX) return;
    client = clent->client;
    producer = G_GetMainSelectedUnit(client);
    if (!G_UnitCanControl(client, producer) || !producer->build || !producer->build->training) return;
    index = (DWORD)parsed;
    if (!G_CancelTrainingQueueItem(producer, index, true)) return;
    Get_Portrait_f(clent);
    Get_Commands_f(clent);
}

CLIENTCOMMAND(DropItem) {
    LPEDICT unit;
    LONG slot;

    if (!clent || !clent->client || argc < 2) {
        return;
    }
    unit = G_GetMainSelectedUnit(clent->client);
    slot = atoi(argv[1]);
    if (!G_UnitCanControl(clent->client, unit) || slot < 0 || (DWORD)slot >= G_InventoryCapacity(unit)) {
        return;
    }
    G_DropItem(unit, (DWORD)slot);
}

static void G_PublishEndCinematicForHumans(LPEDICT clent, BOOL debug_log) {
    if (!clent) return;
    if (debug_log) {
        fprintf(stderr,
                "Client cancel command: player=%u edict=%u time=%u\n",
                clent->client ? (unsigned)clent->client->ps.number : 999u,
                (unsigned)clent->s.number,
                (unsigned)G_Time());
    }
    G_PublishEvent(clent, EVENT_PLAYER_END_CINEMATIC);
    if (!level.mapinfo) return;

    FOR_LOOP(i, game.max_clients) {
        LPEDICT ent = G_GetPlayerEntityByNumber(i);
        if (!ent || ent == clent || level.mapinfo->players[i].playerType != kPlayerTypeHuman)
            continue;
        if (debug_log) {
            fprintf(stderr,
                    "Client cancel command: also publishing for human player=%u edict=%u\n",
                    (unsigned)i,
                    (unsigned)ent->s.number);
        }
        G_PublishEvent(ent, EVENT_PLAYER_END_CINEMATIC);
    }
}

CLIENTCOMMAND(Cancel) {
    if (G_CancelBuildPlacement(clent)) {
        return;
    }
    G_PublishEndCinematicForHumans(clent, true);
}

void UI_ShowQuest(LPEDICT ent, LPCQUEST quest);

CLIENTCOMMAND(Quests) {
    UI_ShowQuests(clent);
}

CLIENTCOMMAND(Log) {
    UI_ShowLog(clent);
}

CLIENTCOMMAND(HideGameResult) {
    DWORD result = clent && clent->client ? clent->client->ps.stats[PLAYERSTATE_GAME_RESULT] : 3;
    G_GameResultDebug("command hidegameresult ent=%u result=%u",
        clent ? (unsigned)clent->s.number : 0u, (unsigned)result);
    UI_HideGameResult(clent);
    if (result == 0 && G_IsSinglePlayer() && level.vm) {
        /* The fallback has no copy of Blizzard.j's bj_changeLevelMapName. Let
         * the stock continuation own EndGame vs ChangeLevel when available. */
        /* The stock dialog-button action runs even while CustomVictoryDialogBJ
         * has the single-player simulation paused.  Running this as a queued
         * coroutine would strand it behind that pause, so execute the Blizzard.j
         * continuation synchronously from the button command. */
        G_GameResultDebug("command hidegameresult calling CustomVictoryOkBJ synchronously");
        jass_callbyname(level.vm, "CustomVictoryOkBJ", false);
    }
}

CLIENTCOMMAND(GameResultRestart) {
    (void)clent; (void)argc; (void)argv;
    G_GameResultDebug("command gameresult_restart");
    G_RequestRestartGame(true);
}

CLIENTCOMMAND(GameResultLoad) {
    (void)clent; (void)argc; (void)argv;
    G_GameResultDebug("command gameresult_load");
    G_RequestLoadGameMenu();
}

CLIENTCOMMAND(GameResultQuit) {
    (void)clent; (void)argc; (void)argv;
    G_GameResultDebug("command gameresult_quit single_player=%u", (unsigned)G_IsSinglePlayer());
    if (G_IsSinglePlayer()) level.setup.difficulty = level.setup.default_difficulty;
    G_RequestEndGame(true);
}

/* F10 and the authored Menu button share this route. Submenu transitions
 * replace the same unique modal window, so the client keeps pause ownership
 * until the menu is actually closed. */
CLIENTCOMMAND(Menu) {
    (void)argc; (void)argv;
    UI_ShowMainMenu(clent);
}

CLIENTCOMMAND(MenuEndGame) {
    (void)argc; (void)argv;
    UI_ShowGameMenuEndGame(clent);
}

CLIENTCOMMAND(MenuConfirmExit) {
    (void)argc; (void)argv;
    UI_ShowGameMenuConfirmExit(clent);
}

CLIENTCOMMAND(Resume) {
    (void)argc; (void)argv;
    G_SetClientModal(clent, WC3_MODAL_CLIENT, false);
}

CLIENTCOMMAND(Pause) {
    if (argc < 2) return;
    G_SetClientModal(clent, WC3_MODAL_CLIENT, atoi(argv[1]) != 0);
}

CLIENTCOMMAND(Allies) {
    (void)argc;
    (void)argv;
    UI_ShowAllies(clent);
}

CLIENTCOMMAND(AlliesToggle) {
    if (argc < 3 || !G_DebugIsNumber(argv[1]) || !G_DebugIsNumber(argv[2])) return;
    UI_AlliesToggle(clent, (DWORD)atoi(argv[1]), (PLAYERALLIANCE)atoi(argv[2]));
}

CLIENTCOMMAND(AlliesToggleVictory) {
    (void)argc; (void)argv;
    UI_AlliesToggleVictory(clent);
}

CLIENTCOMMAND(AlliesAccept) {
    (void)argc; (void)argv;
    UI_AlliesAccept(clent);
}

CLIENTCOMMAND(AlliesCancel) {
    (void)argc; (void)argv;
    UI_AlliesCancel(clent);
}

static void G_CheatPrintf(LPEDICT clent, LPCSTR fmt, ...) {
    char text[1024];
    va_list args;

    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);

    fprintf(stderr, "%s\n", text);
    /* In-engine tests and reserved player slots can issue commands before the
     * client transport is connected. Console feedback is presentation-only,
     * so defer the packet instead of writing into an uninitialized multicast
     * buffer; stderr still records the command result. */
    if (clent && clent->client && clent->client->connected && gi.Write && gi.unicast) {
        LONG opcode = svc_console_print;
        gi.Write(PF_BYTE, &opcode);
        gi.Write(PF_STRING, text);
        gi.unicast(clent);
    }
}

static LPQUEST G_QuestByOrdinal(DWORD ordinal) {
    FOR_EACH_QUEST(q) {
        if (ordinal == 0) return q;
        ordinal--;
    }
    return NULL;
}

static void G_CheatCompleteQuest(LPQUEST quest) {
    if (!quest) return;
    FOR_EACH_QUESTITEM(quest, item) item->completed = true;
    quest->completed = true;
}

static void G_CheatListQuests(LPEDICT clent) {
    DWORD ordinal = 0;

    FOR_EACH_QUEST(q) {
        G_CheatPrintf(clent,
                "WC3: quest %u slot=%u completed=%u failed=%u discovered=%u enabled=%u required=%u title=%s",
                (unsigned)ordinal++,
                (unsigned)(q - level.quests),
                (unsigned)q->completed,
                (unsigned)q->failed,
                (unsigned)q->discovered,
                (unsigned)q->enabled,
                (unsigned)q->required,
                q->title && *q->title ? q->title : "(untitled)");
    }
    if (!ordinal) G_CheatPrintf(clent, "WC3: no quests are currently allocated");
}

static void G_CheatCompleteQuestCommand(LPEDICT clent, DWORD argc, LPCSTR argv[]) {
    DWORD index;
    DWORD count = 0;

    if (argc < 3) {
        G_CheatPrintf(clent, "WC3: usage: quest complete <index|all>");
        return;
    }
    if (!strcasecmp(argv[2], "all")) {
        FOR_EACH_QUEST(q) {
            G_CheatCompleteQuest(q);
            count++;
        }
        G_CheatPrintf(clent, "WC3: completed %u quest%s and their objectives",
                (unsigned)count, count == 1 ? "" : "s");
        return;
    }
    if (!G_DebugIsNumber(argv[2]) || argv[2][0] == '-') {
        G_CheatPrintf(clent, "WC3: quest index must be a non-negative integer or 'all'");
        return;
    }
    index = (DWORD)strtoul(argv[2], NULL, 10);
    LPQUEST quest = G_QuestByOrdinal(index);
    if (!quest) {
        G_CheatPrintf(clent, "WC3: quest %u does not exist", (unsigned)index);
        return;
    }
    G_CheatCompleteQuest(quest);
    G_CheatPrintf(clent, "WC3: completed quest %u and its objectives", (unsigned)index);
}

CLIENTCOMMAND(Quest) {
    DWORD index;

    if (argc < 2 || !argv[1] || !*argv[1]) return;
    if (!strcasecmp(argv[1], "list")) {
        if (!G_CheatsEnabled()) {
            G_CheatPrintf(clent, "WC3: cheats are disabled; set sv_cheats 1");
            return;
        }
        G_CheatListQuests(clent);
        return;
    }
    if (!strcasecmp(argv[1], "complete")) {
        if (!G_CheatsEnabled()) {
            G_CheatPrintf(clent, "WC3: cheats are disabled; set sv_cheats 1");
            return;
        }
        G_CheatCompleteQuestCommand(clent, argc, argv);
        return;
    }
    if (!G_DebugIsNumber(argv[1]) || argv[1][0] == '-') return;
    index = (DWORD)strtoul(argv[1], NULL, 10);
    LPQUEST quest = G_QuestByOrdinal(index);
    if (quest) UI_ShowQuest(clent, quest);
}

static BOOL G_TriggerFunctionMatches(LPCSTR filter, struct jass_function const *func) {
    LPCSTR name = jass_functionname(func);
    return !filter || !*filter || (name && strstr(name, filter));
}

static BOOL G_TriggerMatches(LPCSTR filter, LPTRIGGER trigger) {
    if (!filter || !*filter) return true;
    FOR_EACH_LIST(TRIGGERCONDITION, condition, trigger->conditions) {
        if (G_TriggerFunctionMatches(filter, condition->expr)) return true;
    }
    FOR_EACH_LIST(TRIGGERACTION, action, trigger->actions) {
        if (G_TriggerFunctionMatches(filter, action->func)) return true;
    }
    return false;
}

static void G_CheatPrintTriggerFunctions(LPEDICT clent, LPTRIGGER trigger) {
    FOR_EACH_LIST(TRIGGERCONDITION, condition, trigger->conditions) {
        LPCSTR name = jass_functionname(condition->expr);
        G_CheatPrintf(clent, "    condition: %s", name ? name : "(anonymous)");
    }
    FOR_EACH_LIST(TRIGGERACTION, action, trigger->actions) {
        LPCSTR name = jass_functionname(action->func);
        G_CheatPrintf(clent, "    action:    %s", name ? name : "(anonymous)");
    }
}

static void G_CheatListTriggers(LPEDICT clent, LPCSTR filter) {
    DWORD shown = 0;

    if (!level.vm) {
        G_CheatPrintf(clent, "WC3: no active JASS VM");
        return;
    }
    FOR_LOOP(i, level.num_triggers) {
        LPTRIGGER trigger = &level.triggers[i];
        if (!G_TriggerMatches(filter, trigger)) continue;
        G_CheatPrintf(clent, "WC3: trigger %u %s",
                (unsigned)i, trigger->disabled ? "disabled" : "enabled");
        G_CheatPrintTriggerFunctions(clent, trigger);
        shown++;
    }
    if (!shown) {
        G_CheatPrintf(clent, "WC3: no triggers%s%s%s",
                filter && *filter ? " matching '" : " are allocated",
                filter && *filter ? filter : "",
                filter && *filter ? "'" : "");
    }
}

static BOOL G_CheatFireTrigger(LPEDICT clent, DWORD index, BOOL use_selected, LPCSTR label) {
    LPEDICT unit = NULL;

    if (!level.vm) {
        G_CheatPrintf(clent, "WC3: no active JASS VM");
        return false;
    }
    if (index >= level.num_triggers) {
        G_CheatPrintf(clent, "WC3: trigger %u does not exist", (unsigned)index);
        return false;
    }
    if (use_selected) {
        unit = clent && clent->client ? G_GetMainSelectedUnit(clent->client) : NULL;
        if (!unit) {
            G_CheatPrintf(clent, "WC3: %s %u selected requires a selected unit",
                    label ? label : "trigger fire", (unsigned)index);
            return false;
        }
    }

    G_CheatPrintf(clent, "WC3: %s trigger %u directly%s",
            label ? label : "firing",
            (unsigned)index,
            unit ? " with selected-unit event context" : "");
    /* This is deliberately TriggerExecute-style debug behavior: execute the
     * authored actions even if the trigger is disabled and without evaluating
     * its conditions. Campaign/cinematic debug commands need to reach authored
     * actions that may not yet be armed by normal map progression. */
    jass_executetrigger(level.vm, &level.triggers[index], unit);
    return true;
}

CLIENTCOMMAND(Trigger) {
    DWORD index;
    BOOL use_selected = false;

    if (!G_CheatsEnabled()) {
        G_CheatPrintf(clent, "WC3: cheats are disabled; set sv_cheats 1");
        return;
    }
    if (argc < 2) {
        G_CheatPrintf(clent, "WC3: usage: trigger list [filter] | trigger fire <index> [selected]");
        return;
    }
    if (!strcasecmp(argv[1], "list")) {
        G_CheatListTriggers(clent, argc >= 3 ? argv[2] : NULL);
        return;
    }
    if (strcasecmp(argv[1], "fire")) {
        G_CheatPrintf(clent, "WC3: usage: trigger list [filter] | trigger fire <index> [selected]");
        return;
    }
    if (argc < 3 || !G_DebugIsNumber(argv[2]) || argv[2][0] == '-') {
        G_CheatPrintf(clent, "WC3: trigger fire requires a non-negative trigger index");
        return;
    }
    if (argc >= 4) {
        if (strcasecmp(argv[3], "selected")) {
            G_CheatPrintf(clent, "WC3: optional trigger context must be 'selected'");
            return;
        }
        use_selected = true;
    }
    index = (DWORD)strtoul(argv[2], NULL, 10);
    G_CheatFireTrigger(clent, index, use_selected, "firing");
}

static BOOL G_StringContainsNoCase(LPCSTR text, LPCSTR needle) {
    size_t needle_len;

    if (!text || !needle || !*needle) return false;
    needle_len = strlen(needle);
    while (*text) {
        if (!strncasecmp(text, needle, needle_len)) return true;
        text++;
    }
    return false;
}

static BOOL G_FunctionNameContainsAny(LPCSTR name, LPCSTR const words[], DWORD count) {
    if (!name) return false;
    FOR_LOOP(i, count) {
        if (G_StringContainsNoCase(name, words[i])) return true;
    }
    return false;
}

static BOOL G_CinematicFunctionName(LPCSTR name) {
    static LPCSTR const markers[] = {
        "cinematic", "cutscene", "intro", "outro", "ending", "interlude"
    };
    static LPCSTR const helpers[] = {
        "skip", "time_stop", "timestop", "cheat"
    };

    if (!name || G_FunctionNameContainsAny(name, helpers, sizeof(helpers) / sizeof(helpers[0]))) {
        return false;
    }
    return G_FunctionNameContainsAny(name, markers, sizeof(markers) / sizeof(markers[0]));
}

static BOOL G_TriggerLooksCinematic(LPTRIGGER trigger) {
    /* Classify by action names. Generated condition/helper names frequently
     * inherit words such as Intro without actually starting a cutscene. */
    FOR_EACH_LIST(TRIGGERACTION, action, trigger->actions) {
        if (G_CinematicFunctionName(jass_functionname(action->func))) return true;
    }
    return false;
}

static void G_CheatListCinematics(LPEDICT clent, LPCSTR filter) {
    DWORD shown = 0;

    if (!level.vm) {
        G_CheatPrintf(clent, "WC3: no active JASS VM");
        return;
    }
    FOR_LOOP(i, level.num_triggers) {
        LPTRIGGER trigger = &level.triggers[i];
        if (!G_TriggerLooksCinematic(trigger)) continue;
        if (filter && *filter && !G_TriggerMatches(filter, trigger)) continue;
        G_CheatPrintf(clent, "WC3: cinematic candidate trigger %u %s",
                (unsigned)i, trigger->disabled ? "disabled" : "enabled");
        G_CheatPrintTriggerFunctions(clent, trigger);
        shown++;
    }
    if (!shown) {
        G_CheatPrintf(clent,
                "WC3: no cinematic trigger candidates%s%s%s; use 'trigger list [filter]' to inspect all map triggers",
                filter && *filter ? " matching '" : "",
                filter && *filter ? filter : "",
                filter && *filter ? "'" : "");
    }
}

static BOOL G_ObjectiveFunctionName(LPCSTR name) {
    static LPCSTR const rejects[] = {
        "cheat", "defeat", "skip", "cinematic", "cutscene", "intro",
        "outro", "ending", "interlude", "time_stop", "timestop"
    };
    BOOL questish;
    BOOL completion;

    if (!name || G_FunctionNameContainsAny(name, rejects, sizeof(rejects) / sizeof(rejects[0]))) {
        return false;
    }
    if (G_StringContainsNoCase(name, "victory")) return true;

    questish = G_StringContainsNoCase(name, "quest") || G_StringContainsNoCase(name, "objective");
    completion = G_StringContainsNoCase(name, "complete") ||
                 G_StringContainsNoCase(name, "finish") ||
                 G_StringContainsNoCase(name, "done");
    return questish && completion;
}

static BOOL G_TriggerLooksObjective(LPTRIGGER trigger) {
    FOR_EACH_LIST(TRIGGERACTION, action, trigger->actions) {
        if (G_ObjectiveFunctionName(jass_functionname(action->func))) return true;
    }
    return false;
}

static void G_CheatListObjectives(LPEDICT clent, LPCSTR filter) {
    DWORD shown = 0;

    if (!level.vm) {
        G_CheatPrintf(clent, "WC3: no active JASS VM");
        return;
    }
    FOR_LOOP(i, level.num_triggers) {
        LPTRIGGER trigger = &level.triggers[i];
        if (!G_TriggerLooksObjective(trigger)) continue;
        if (filter && *filter && !G_TriggerMatches(filter, trigger)) continue;
        G_CheatPrintf(clent, "WC3: objective completion candidate trigger %u %s",
                (unsigned)i, trigger->disabled ? "disabled" : "enabled");
        G_CheatPrintTriggerFunctions(clent, trigger);
        shown++;
    }
    if (!shown) {
        G_CheatPrintf(clent,
                "WC3: no objective completion trigger candidates%s%s%s; use 'trigger list [filter]' to inspect all map triggers",
                filter && *filter ? " matching '" : "",
                filter && *filter ? filter : "",
                filter && *filter ? "'" : "");
    }
}

CLIENTCOMMAND(Objective) {
    DWORD index;
    BOOL use_selected = false;

    if (!G_CheatsEnabled()) {
        G_CheatPrintf(clent, "WC3: cheats are disabled; set sv_cheats 1");
        return;
    }
    if (argc < 2) {
        G_CheatPrintf(clent, "WC3: usage: objective list [filter] | objective complete <trigger-index> [selected]");
        return;
    }
    if (!strcasecmp(argv[1], "list")) {
        G_CheatListObjectives(clent, argc >= 3 ? argv[2] : NULL);
        return;
    }
    if (strcasecmp(argv[1], "complete")) {
        G_CheatPrintf(clent, "WC3: usage: objective list [filter] | objective complete <trigger-index> [selected]");
        return;
    }
    if (argc < 3 || !G_DebugIsNumber(argv[2]) || argv[2][0] == '-') {
        G_CheatPrintf(clent, "WC3: objective complete requires a non-negative trigger index");
        return;
    }
    if (argc >= 4) {
        if (strcasecmp(argv[3], "selected")) {
            G_CheatPrintf(clent, "WC3: optional objective context must be 'selected'");
            return;
        }
        use_selected = true;
    }
    index = (DWORD)strtoul(argv[2], NULL, 10);
    G_CheatFireTrigger(clent, index, use_selected, "completing objective via");
}

CLIENTCOMMAND(Cinematic) {
    DWORD index;
    BOOL use_selected = false;

    if (!G_CheatsEnabled()) {
        G_CheatPrintf(clent, "WC3: cheats are disabled; set sv_cheats 1");
        return;
    }
    if (argc < 2) {
        G_CheatPrintf(clent,
                "WC3: usage: cinematic list [filter] | cinematic play <trigger-index> [selected] | cinematic stop");
        return;
    }
    if (!strcasecmp(argv[1], "list")) {
        G_CheatListCinematics(clent, argc >= 3 ? argv[2] : NULL);
        return;
    }
    if (!strcasecmp(argv[1], "stop")) {
        /* Match Escape instead of forcing presentation state back to gameplay.
         * The map-authored EVENT_PLAYER_END_CINEMATIC handler owns skip flags,
         * camera/unit cleanup, control restoration, and coroutine termination. */
        G_PublishEndCinematicForHumans(clent, false);
        G_CheatPrintf(clent, "WC3: published end-cinematic event for human players");
        return;
    }
    if (strcasecmp(argv[1], "play")) {
        G_CheatPrintf(clent,
                "WC3: usage: cinematic list [filter] | cinematic play <trigger-index> [selected] | cinematic stop");
        return;
    }
    if (argc < 3 || !G_DebugIsNumber(argv[2]) || argv[2][0] == '-') {
        G_CheatPrintf(clent, "WC3: cinematic play requires a non-negative trigger index");
        return;
    }
    if (argc >= 4) {
        if (strcasecmp(argv[3], "selected")) {
            G_CheatPrintf(clent, "WC3: optional cinematic context must be 'selected'");
            return;
        }
        use_selected = true;
    }
    index = (DWORD)strtoul(argv[2], NULL, 10);
    G_CheatFireTrigger(clent, index, use_selected, "playing cinematic candidate");
}

CLIENTCOMMAND(Jass) {
    if (!G_CheatsEnabled()) {
        G_CheatPrintf(clent, "WC3: cheats are disabled; set sv_cheats 1");
        return;
    }
    if (!level.vm) {
        G_CheatPrintf(clent, "WC3: no active JASS VM");
        return;
    }
    if (argc != 2 || !argv[1] || !*argv[1]) {
        G_CheatPrintf(clent, "WC3: usage: jass <zero-argument-function-name>");
        return;
    }
    G_CheatPrintf(clent, "WC3: starting JASS function %s as a coroutine", argv[1]);
    jass_callbyname(level.vm, argv[1], true);
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

static LPEDICT G_PortraitCameraUnit(LPEDICT clent, DWORD argc, LPCSTR argv[]) {
    DWORD number;
    LPEDICT target;
    if (!clent || !clent->client || argc < 2 || !G_DebugIsNumber(argv[1])) return NULL;
    number = (DWORD)atoi(argv[1]);
    if (number >= globals.num_edicts) return NULL;
    target = &globals.edicts[number];
    if (!target->inuse || G_GetMainSelectedUnit(clent->client) != target || !G_IsEntitySelected(clent->client, target)) return NULL;
    return target;
}

static void CMD_PortraitCameraDown(LPEDICT clent, DWORD argc, LPCSTR argv[]) {
    LPEDICT target = G_PortraitCameraUnit(clent, argc, argv);
    if (!target || clent->client->no_control || clent->client->camera.target_controller) return;
    G_ClientSetCameraPosition(clent, &target->s.origin2);
    clent->client->camera.target_controller = target;
    clent->client->camera.target_offset = (VECTOR2){ 0, 0 };
}

static void CMD_QuickCamera(LPEDICT clent, DWORD argc, LPCSTR argv[]) {
    (void)argc;
    (void)argv;
    if (!clent || !clent->client || !clent->client->camera.quick_position_set) return;
    G_ClientSetCameraPosition(clent, &clent->client->camera.quick_position);
}

static void CMD_PortraitCameraUp(LPEDICT clent, DWORD argc, LPCSTR argv[]) {
    DWORD number;
    if (!clent || !clent->client || argc < 2 || !G_DebugIsNumber(argv[1])) return;
    number = (DWORD)atoi(argv[1]);
    if (number >= globals.num_edicts || clent->client->camera.target_controller != &globals.edicts[number]) return;
    G_ClearCameraTarget(clent->client, "CMD_PortraitCameraUp");
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
    location = (VECTOR2){ client->ps.vieworigin.x, client->ps.vieworigin.y };
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
} clientCommand_t;

clientCommand_t clientCommands[] = {
    { "give", CMD_Give },
    { "god", CMD_God },
    { "kill", CMD_Kill },
    { "win", CMD_Win },
    { "lose", CMD_Lose },
    { "day", CMD_Day },
    { "night", CMD_Night },
    { "button", CMD_Button },
    { "autocast", CMD_Autocast },
    { "research", CMD_Research },
    { "inventory", CMD_Inventory },
    { "dropitem", CMD_DropItem },
    { "select", CMD_Select },
    { "focus", CMD_Focus },
    { "+portraitcamera", CMD_PortraitCameraDown },
    { "-portraitcamera", CMD_PortraitCameraUp },
    { "quickcamera", CMD_QuickCamera },
    { "herobutton", CMD_HeroButton },
    { "herokey", CMD_HeroKey },
    { "idleworker", CMD_IdleWorker },
    { "point", CMD_Point },
    { "smart", CMD_Smart },
    { "smartpoint", CMD_SmartPoint },
    { "cancel", CMD_Cancel },
    { "canceltrain", CMD_CancelTrain },
    { "quests", CMD_Quests },
    { "quest", CMD_Quest },
    { "trigger", CMD_Trigger },
    { "objective", CMD_Objective },
    { "cinematic", CMD_Cinematic },
    { "jass", CMD_Jass },
    { "log", CMD_Log },
    { "hidegameresult", CMD_HideGameResult },
    { "gameresult_restart", CMD_GameResultRestart },
    { "gameresult_load", CMD_GameResultLoad },
    { "gameresult_quit", CMD_GameResultQuit },
    { "debugspawn", CMD_DebugSpawn },
    { "menu", CMD_Menu },
    { "menu_endgame", CMD_MenuEndGame },
    { "menu_confirm_exit", CMD_MenuConfirmExit },
    { "resume", CMD_Resume },
    { "pause", CMD_Pause },
    { "allies", CMD_Allies },
    { "allies_toggle", CMD_AlliesToggle },
    { "allies_toggle_victory", CMD_AlliesToggleVictory },
    { "allies_accept", CMD_AlliesAccept },
    { "allies_cancel", CMD_AlliesCancel },
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
    VECTOR2 clamped;

    if (ent->client->no_control)
        return;
    clamped = G_ClampCameraPosition(ent->client, position);
    G_ClearCameraTarget(ent->client, "G_ClientSetCameraPosition");
    ent->client->camera.old_state = ent->client->camera.state;
    ent->client->camera.state.position = clamped;
    ent->client->camera.start_time = G_Time();
    ent->client->camera.end_time = ent->client->camera.start_time;
}
