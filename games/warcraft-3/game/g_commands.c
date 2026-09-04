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
    if (owner == PLAYER_NEUTRAL_AGGRESSIVE) {
        return SELECT_RELATION_ENEMY;
    }
    if (owner == PLAYER_NEUTRAL_PASSIVE) {
        return SELECT_RELATION_NEUTRAL;
    }
    if (viewer >= MAX_PLAYERS || owner >= MAX_PLAYERS) {
        return SELECT_RELATION_ENEMY;
    }
    alliances = level.alliances[viewer][owner];
    if (!(alliances & (1 << ALLIANCE_PASSIVE))) {
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
    if (owner == PLAYER_NEUTRAL_AGGRESSIVE || owner == PLAYER_NEUTRAL_PASSIVE ||
        owner >= MAX_PLAYERS || client->ps.number >= MAX_PLAYERS) {
        return false;
    }
    alliances = level.alliances[client->ps.number][owner];
    return (alliances & (1 << ALLIANCE_PASSIVE)) != 0 &&
           (alliances & (1 << ALLIANCE_SHARED_CONTROL)) != 0;
}

void G_UpdateClientSelections(void) {
    FOR_LOOP(i, game.max_clients) {
        LPGAMECLIENT client = game.clients + i;
        LPEDICT clent;
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
        clent = G_GetPlayerEntityByNumber(client->ps.number);
        if (client->connected && clent && clent->client == client) {
            Get_Portrait_f(clent);
            Get_Commands_f(clent);
        } else {
            G_InvalidateCommands(client);
        }
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
            /* Selection is authoritative game state; HUD serialization is not
             * valid until ClientBegin has completed for this player slot. */
            if (client->connected) {
                Get_Portrait_f(clent);
                Get_Commands_f(clent);
            } else {
                G_InvalidateCommands(client);
            }
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

    /* Selection membership is unchanged. Only focused-unit presentation and
     * focused-unit commands need to be rebuilt. */
    G_RefreshInventoryLayer(clent);
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

CLIENTCOMMAND(Cancel) {
    if (G_CancelBuildPlacement(clent)) {
        return;
    }
    fprintf(stderr,
            "Client cancel command: player=%u edict=%u time=%u\n",
            clent && clent->client ? (unsigned)clent->client->ps.number : 999u,
            clent ? (unsigned)clent->s.number : 999u,
            (unsigned)G_Time());
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

CLIENTCOMMAND(Quest) {
    DWORD index;

    if (argc < 2 || !argv[1] || !*argv[1]) return;
    index = atoi(argv[1]);
    FOR_EACH_QUEST(q) {
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
