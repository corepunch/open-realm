#include <stdarg.h>
#include <ctype.h>
#include <stdlib.h>

#include "g_local.h"

#define CLIENTCOMMAND(NAME) void CMD_##NAME(edict_t *clent, uint32_t argc, cstring_t argv[])
#define WC3_SELECTION_LIMIT 12
#define WC3_ENEMIES_CLEAR_RADIUS 768.0f

typedef struct {
    cstring_t name;
    bool value;
} cheatToggleValue_t;

static cheatToggleValue_t const cheat_toggle_values[] = {
    { "on", true }, { "1", true }, { "off", false }, { "0", false },
};

/* Focus is presentation/input state within an existing selection, not a saved
 * gameplay relationship. Keep it out of GAMECLIENT so save compatibility does
 * not depend on which multiselect subgroup happened to own the HUD. */
static uint32_t selection_focus[MAX_CLIENTS];
#ifdef BZ_TESTS
static uint32_t test_selection_checks;
void G_ResetTestSelectionChecks(void) { test_selection_checks = 0; }
uint32_t G_GetTestSelectionChecks(void) { return test_selection_checks; }
#define TEST_SELECTION_CHECK() test_selection_checks++
#else
#define TEST_SELECTION_CHECK() ((void)0)
#endif
static bool G_DebugIsNumber(cstring_t text);
static void G_CheatPrintf(edict_t *clent, cstring_t fmt, ...);
static void G_PublishEndCinematicForHumans(edict_t *clent, bool debug_log);

static uint32_t *G_SelectionFocusSlot(gameClient_t *client) {
    int32_t index;

    if (!client || !game.clients) return NULL;
    index = (int32_t)(client - game.clients);
    if (index < 0 || index >= MAX_CLIENTS) return NULL;
    return &selection_focus[index];
}

static int32_t G_SelectionPriority(edict_t const *ent) {
    UnitData_t const *data;

    if (!ent) return 0;
    data = ent->data.UnitData ? ent->data.UnitData : G_UnitData(ent->class_id);
    return data ? data->priority : 0;
}

static int32_t G_SelectionLevel(edict_t const *ent) {
    UnitBalance_t const *balance;

    if (!ent) return 0;
    balance = ent->data.UnitBalance ? ent->data.UnitBalance : G_UnitBalance(ent->class_id);
    return balance ? balance->level : 0;
}

/* OpenRealm's MAKEFOURCC stores the first character in the low byte, while
 * Warsmash's War3ID value stores it in the high byte.  Selection ordering uses
 * Warsmash's numeric War3ID tie-break, so compare a canonical byte-swapped
 * value rather than the native class_id integer. */
static uint32_t G_SelectionRawcodeValue(uint32_t class_id) {
    return ((class_id & 0x000000ffu) << 24) |
           ((class_id & 0x0000ff00u) << 8) |
           ((class_id & 0x00ff0000u) >> 8) |
           ((class_id & 0xff000000u) >> 24);
}

/* Return < 0 when lhs belongs before rhs in Warcraft selection presentation.
 * Warsmash sorts unit type priority, level, then War3ID descending. Equal
 * unit types compare equal so stable callers preserve authoritative entity
 * scan order for otherwise-identical entries. The multiselect panel and Hero
 * shortcuts intentionally share this comparator. */
int32_t G_CompareSelectionOrder(edict_t const *lhs, edict_t const *rhs) {
    int32_t left_value;
    int32_t right_value;
    uint32_t left_rawcode;
    uint32_t right_rawcode;

    left_value = G_SelectionPriority(lhs);
    right_value = G_SelectionPriority(rhs);
    if (left_value != right_value) return left_value > right_value ? -1 : 1;

    left_value = G_SelectionLevel(lhs);
    right_value = G_SelectionLevel(rhs);
    if (left_value != right_value) return left_value > right_value ? -1 : 1;

    left_rawcode = G_SelectionRawcodeValue(lhs ? lhs->class_id : 0);
    right_rawcode = G_SelectionRawcodeValue(rhs ? rhs->class_id : 0);
    if (left_rawcode != right_rawcode) return left_rawcode > right_rawcode ? -1 : 1;
    return 0;
}

uint32_t G_GetOrderedSelectedUnits(gameClient_t *client, edict_t * *out, uint32_t max_out) {
    uint32_t seen = 0;

    if (!client || !out || !max_out) return 0;

    FOR_SELECTED_UNITS(client, ent) {
        uint32_t insert;

        if (seen < max_out) {
            insert = seen;
            out[insert] = ent;
        } else if (G_CompareSelectionOrder(ent, out[max_out - 1]) < 0) {
            /* Keep the best max_out entries even if malformed/scripted state
             * ever exceeds the normal 12-unit authoritative selection cap. */
            insert = max_out - 1;
            out[insert] = ent;
        } else {
            seen++;
            continue;
        }

        while (insert > 0 && G_CompareSelectionOrder(out[insert], out[insert - 1]) < 0) {
            edict_t *swap = out[insert - 1];
            out[insert - 1] = out[insert];
            out[insert] = swap;
            insert--;
        }
        seen++;
    }

    return MIN(seen, max_out);
}

static bool G_TargetModeActive(gameClient_t *client) {
    return client && (client->menu.on_entity_selected || client->menu.on_location_selected);
}

static bool G_CommandQueueRequested(uint32_t argc, cstring_t argv[], uint32_t first_optional) {
    for (uint32_t i = first_optional; i < argc; i++) {
        if (!strcmp(argv[i], "queue")) return true;
    }
    return false;
}

static bool G_SelectionListContains(edict_t *const *selection, uint32_t count, edict_t const *ent) {
    FOR_LOOP(i, count) {
        if (selection[i] == ent) return true;
    }
    return false;
}

static void G_PublishSelectionDelta(gameClient_t *client,
                                    edict_t *const *old_selection,
                                    uint32_t old_count) {
    bool const debug = WC3_TUTORIAL_DEBUG_ENABLED();

    if (!client) return;

    FOR_LOOP(i, old_count) {
        edict_t *ent = old_selection[i];
        if (!G_IsEntitySelected(client, ent)) {
            if (debug) {
                char rawcode[5] = { 0 };
                memcpy(rawcode, &ent->class_id, 4);
                fprintf(stderr,
                    "WC3_QUEST_SELECT publish event=DESELECTED player=%u unit=%u id=%s\n",
                    (unsigned)client->ps.number, (unsigned)ent->s.number, rawcode);
            }
            G_PublishEvent(ent, EVENT_PLAYER_UNIT_DESELECTED);
            G_PublishEvent(ent, EVENT_UNIT_DESELECTED);
        }
    }

    FOR_SELECTED_UNITS(client, ent) {
        if (!G_SelectionListContains(old_selection, old_count, ent)) {
            if (debug) {
                char rawcode[5] = { 0 };
                memcpy(rawcode, &ent->class_id, 4);
                fprintf(stderr,
                    "WC3_QUEST_SELECT publish event=SELECTED player=%u unit=%u id=%s\n",
                    (unsigned)client->ps.number, (unsigned)ent->s.number, rawcode);
            }
            G_PublishEvent(ent, EVENT_PLAYER_UNIT_SELECTED);
            G_PublishEvent(ent, EVENT_UNIT_SELECTED);
        }
    }
}

static bool G_ParseEntityNumber(cstring_t text, uint32_t *number) {
    char *end = NULL;
    unsigned long value;

    if (!text || !*text || !number) return false;
    value = strtoul(text, &end, 10);
    if (!end || *end || value >= globals.num_edicts) return false;
    *number = (uint32_t)value;
    return true;
}

edict_t *G_GetMainSelectedUnit(gameClient_t *client) {
    uint32_t *focus = G_SelectionFocusSlot(client);
    edict_t *ordered[1];

    if (focus && *focus > 0 && *focus < globals.num_edicts) {
        edict_t *ent = &globals.edicts[*focus];
        if (G_IsEntitySelected(client, ent)) return ent;
    }
    if (G_GetOrderedSelectedUnits(client, ordered, sizeof(ordered) / sizeof(ordered[0]))) {
        if (focus) *focus = ordered[0]->s.number;
        return ordered[0];
    }
    if (focus) *focus = 0;
    return NULL;
}


void G_SyncClientSelection(gameClient_t *client) {
    edict_t *clent;
    uint32_t selected[WC3_SELECTION_LIMIT];
    uint32_t count = 0;

    if (!client) return;
    client->selection_dirty = false;
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

    gi.Write(PF_BYTE, &(int32_t){svc_set_selection});
    gi.Write(PF_BYTE, &(int32_t){count});
    FOR_LOOP(i, count) gi.Write(PF_LONG, &(int32_t){selected[i]});
    gi.unicast(clent);

    Get_Portrait_f(clent);
    Get_Commands_f(clent);
}

bool G_FocusSelectedUnit(gameClient_t *client, edict_t *ent) {
    uint32_t *focus = G_SelectionFocusSlot(client);

    if (!focus || !G_IsEntitySelected(client, ent)) return false;
    *focus = ent->s.number;
    return true;
}

bool G_CycleSelectionSubgroup(gameClient_t *client) {
    edict_t *ordered[WC3_SELECTION_LIMIT];
    edict_t *main;
    uint32_t count;
    uint32_t main_index = 0;
    uint32_t next_index;

    if (!client) return false;
    count = G_GetOrderedSelectedUnits(client, ordered, WC3_SELECTION_LIMIT);
    if (count < 2) return false;

    main = G_GetMainSelectedUnit(client);
    if (!main) return false;
    while (main_index < count && ordered[main_index] != main) main_index++;
    if (main_index >= count) main_index = 0;

    /* Equal unit types are contiguous in the Warsmash-compatible selection
     * order. Tab advances to the first unit of the next type subgroup and
     * wraps from the final subgroup back to the first. */
    next_index = main_index + 1;
    while (next_index < count &&
           ordered[next_index]->class_id == ordered[main_index]->class_id) {
        next_index++;
    }
    if (next_index >= count) next_index = 0;

    /* A multiselection containing only one unit type has no other subgroup. */
    if (ordered[next_index]->class_id == ordered[main_index]->class_id) return false;
    return G_FocusSelectedUnit(client, ordered[next_index]);
}

void G_ResetSelectionFocus(gameClient_t *client) {
    uint32_t *focus = G_SelectionFocusSlot(client);
    if (focus) *focus = 0;
}

edict_t *G_GetMainControllableUnit(gameClient_t *client) {
    edict_t *main = G_GetMainSelectedUnit(client);

    if (main && G_UnitCanControl(client, main)) return main;
    FOR_CONTROLLABLE_SELECTED_UNITS(client, ent) {
        return ent;
    }
    return NULL;
}

void G_SelectEntity(gameClient_t *client, edict_t *ent) {
    bool had_selection = false;

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

void G_DeselectEntity(gameClient_t *client, edict_t *ent) {
    uint32_t *focus;

    if (!client || !ent) return;
    focus = G_SelectionFocusSlot(client);
    if (focus && *focus == ent->s.number) *focus = 0;
    ent->selected &= ~(1 << client->ps.number);
}

bool G_IsEntitySelected(gameClient_t *client, edict_t *ent) {
    TEST_SELECTION_CHECK();
    return client && ent && ent->inuse && !M_IsDead(ent) &&
        !(ent->s.flags & EF_NOT_SELECTABLE) && !(ent->s.renderfx & RF_HIDDEN) &&
        (ent->selected & (1 << client->ps.number));
}

selectionRelation_t G_SelectionRelation(uint32_t viewer, edict_t const *ent) {
    uint32_t owner;
    uint32_t alliances;

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

bool G_UnitCanBeSelected(gameClient_t *client, edict_t const *ent) {
    if (!client || !ent || !ent->inuse || !(ent->svflags & SVF_MONSTER)) {
        return false;
    }
    if ((ent->svflags & SVF_DEADMONSTER) || ent->health.value <= 0.0f ||
        (ent->s.flags & EF_NOT_SELECTABLE)) {
        return false;
    }
    if ((ent->s.renderfx & RF_HIDDEN) &&
        (!S_UnitUsesInvisibilityRenderFlag(ent) ||
         S_UnitIsInvisibleToPlayer(ent, client->ps.number))) {
        return false;
    }
    return G_FowPlayerCanHoverEntity(client->ps.number, ent);
}

bool G_UnitCanControl(gameClient_t *client, edict_t const *ent) {
    uint32_t owner;
    uint32_t alliances;

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
        gameClient_t *client = game.clients + i;
        bool changed = false;
        uint32_t bit = 1 << client->ps.number;

        /* Inspect the raw bit here rather than FOR_SELECTED_UNITS.  The latter
         * deliberately hides dead/unselectable entities, while this pass must
         * clear stale selection bits after visibility/selectability changes. */
        FILTER_EDICTS(ent, ent->selected & bit) {
            if (!G_UnitCanBeSelected(client, ent)) {
                G_DeselectEntity(client, ent);
                changed = true;
            }
        }
        if (!changed && !client->selection_dirty) {
            continue;
        }
        G_SyncClientSelection(client);
    }
}

typedef struct {
    int32_t entity;
    uint32_t spawn_time, selected_sound_count, generation;
    bool valid;
} selectionSoundState_t;

typedef struct unitResponse_s {
    struct unitResponse_s *next;
    uint32_t request, entity, spawn_time, owner, generation;
    int sound;
    bool accepted, started;
} unitResponse_t;

static selectionSoundState_t selection_sound_state[MAX_PLAYERS];
static unitResponse_t *unit_responses;
static uint32_t response_serial, selection_serial;

void G_ResetSelectionSoundState(void) {
    while (unit_responses) {
        unitResponse_t *next = unit_responses->next;
        free(unit_responses); unit_responses = next;
    }
    FOR_LOOP(i, globals.num_edicts) if (globals.edicts) globals.edicts[i].sound.pending = 0;
    memset(selection_sound_state, 0, sizeof(selection_sound_state));
    /* Keep serials across maps/save-load so old reliable replies cannot match. */
}

bool G_UnitResponseTalking(edict_t const *ent) {
    for (unitResponse_t *r = unit_responses; ent && r; r = r->next)
        if (r->entity == ent->s.number && r->spawn_time == ent->spawn_time &&
            r->owner == ent->s.player && r->started) return true;
    return false;
}

static void G_DirtyResponsePortrait(edict_t const *ent) {
    gameClient_t *client;
    if (!ent || ent->s.player >= MAX_PLAYERS) return;
    client = G_GetPlayerClientByNumber(ent->s.player);
    if (client && G_GetMainControllableUnit(client) == ent) client->presentation_dirty = true;
}

void G_ClearUnitResponses(edict_t const *ent) {
    for (unitResponse_t **link = &unit_responses; *link;) {
        unitResponse_t *r = *link;
        if (r->entity != ent->s.number) { link = &r->next; continue; }
        *link = r->next; free(r);
    }
    G_DirtyResponsePortrait(ent);
}

uint32_t G_UnitResponseRequest(edict_t const *ent, int sound) {
    for (unitResponse_t *r = unit_responses; ent && r; r = r->next)
        if (r->entity == ent->s.number && r->spawn_time == ent->spawn_time &&
            !r->accepted && r->sound == sound) return r->request;
    return 0;
}

bool G_QueueUnitResponseSound(edict_t *ent, int sound) {
    if (!ent || !sound || ent->s.player >= MAX_PLAYERS || ent->s.number >= MAX_ENTITIES) return false;
    for (unitResponse_t *r = unit_responses; r; r = r->next)
        if (r->entity == ent->s.number && r->spawn_time == ent->spawn_time && !r->accepted) return false;
    unitResponse_t *r = calloc(1, sizeof(*r));
    if (!r) { fprintf(stderr, "G_QueueUnitResponseSound: out of memory\n"); return false; }
    if (!++response_serial) ++response_serial;
    *r = (unitResponse_t){ .next = unit_responses, .request = response_serial,
        .entity = ent->s.number, .spawn_time = ent->spawn_time, .owner = ent->s.player, .sound = sound };
    unit_responses = r;
    ent->sound.pending = sound;
    return true;
}

void G_UpdateUnitResponsePresentation(void) {
    /* Disconnect, removal and ownership changes retire outstanding requests.
     * Admission/start/end timing never depends on simulation time or duration. */
    for (unitResponse_t **link = &unit_responses; *link;) {
        unitResponse_t *r = *link;
        edict_t *ent = g_edicts + r->entity;
        bool connected = false;
        for (uint32_t i = 0; !connected && i < game.max_clients; i++)
            connected = game.clients[i].connected && game.clients[i].ps.number == r->owner;
        if (ent->inuse && ent->spawn_time == r->spawn_time && ent->s.player == r->owner && connected) { link = &r->next; continue; }
        if (!r->accepted && ent->spawn_time == r->spawn_time && ent->sound.pending == r->sound) ent->sound.pending = 0;
        *link = r->next; free(r); G_DirtyResponsePortrait(ent);
    }
}

static void G_ResponseEvent(edict_t *player, uint32_t user, uint32_t request, uint32_t event) {
    if (!player || !player->client || !player->client->connected || user >= globals.num_edicts) return;
    for (unitResponse_t **link = &unit_responses; *link; link = &(*link)->next) {
        unitResponse_t *r = *link;
        edict_t *ent = g_edicts + user;
        if (r->request != request || r->entity != user) continue;
        if (r->owner != player->client->ps.number || !ent->inuse ||
            ent->spawn_time != r->spawn_time || ent->s.player != r->owner) return;
        if (!r->accepted && (event == SOUND_ACCEPTED || event == SOUND_REJECTED) && ent->sound.pending == r->sound)
            ent->sound.pending = 0;
        if (event == SOUND_ACCEPTED && !r->accepted) {
            r->accepted = true;
            selectionSoundState_t *state = selection_sound_state + r->owner;
            if (r->generation && state->generation == r->generation) state->selected_sound_count++;
            G_AcceptSoundVariant(r->sound, r->owner);
        } else if (event == SOUND_STARTED && r->accepted) {
            r->started = true; G_DirtyResponsePortrait(ent);
        } else if ((event == SOUND_REJECTED && !r->accepted) || (event == SOUND_ENDED && r->accepted)) {
            *link = r->next; free(r); G_DirtyResponsePortrait(ent);
        }
        return;
    }
}

CLIENTCOMMAND(SoundEvent) {
    uint32_t value[3];
    if (argc != 4) return;
    FOR_LOOP(i, 3) {
        char *end;
        if (!argv[i+1][0] || !isdigit((unsigned char)argv[i+1][0])) return;
        unsigned long n = strtoul(argv[i+1], &end, 10);
        if (*end || n > UINT32_MAX) return;
        value[i] = (uint32_t)n;
    }
    G_ResponseEvent(clent, value[0], value[1], value[2]);
}

static selectionSoundState_t *G_SelectionSoundState(edict_t *ent, bool reset) {
    if (!ent || ent->s.player >= MAX_PLAYERS) return NULL;
    selectionSoundState_t *state = selection_sound_state + ent->s.player;
    if (reset || !state->valid || state->entity != (int32_t)ent->s.number || state->spawn_time != ent->spawn_time) {
        if (!++selection_serial) ++selection_serial;
        *state = (selectionSoundState_t){ .entity = ent->s.number, .spawn_time = ent->spawn_time,
            .generation = selection_serial, .valid = true };
    }
    return state;
}

static void G_ResetSelectionResponseForUnit(edict_t *ent) {
    selectionSoundState_t *state = G_SelectionSoundState(ent, true);
    if (state) state->selected_sound_count = 0;
}

static int G_RandomResponseSound(edict_t const *ent, uint16_t const *sounds, uint32_t count) {
    int index = 0;
    if (count) for (int tries = 0; tries < 11; tries++) {
        index = sounds[rand() % count];
        if (count == 1 || !G_SoundVariantIsLast(index, ent->s.player)) break;
    }
    return index;
}

/* Client commands arrive before G_RunEntities clears the previous snapshot's
 * event, so retain the chosen acknowledgement until that frame begins. Warsmash
 * uses three normal What responses before walking the Pissed bank in order. */
void G_QueueSelectionSound(edict_t *ent, bool reset_sequence) {
    selectionSoundState_t *state;
    cstring_t label;
    uint32_t pissed_count, pissed_index;
    int sound = 0;

    if (!ent || !(state = G_SelectionSoundState(ent, reset_sequence))) return;
    if (ent->construction.active) {
        gameClient_t *client = G_GetPlayerClientByNumber(ent->s.player);
        cstring_t alias = client ? Theme_PlayerString(client, "ConstructingBuilding", NULL) : NULL;
        int sound_index = G_UISoundIndex(alias);
        if (sound_index) G_QueueUnitResponseSound(ent, sound_index);
        state->selected_sound_count = 0;
        return;
    }

    label = ent->data.UnitUI ? ent->data.UnitUI->soundLabel : NULL;
    pissed_count = G_UnitAckSoundVariantCount(label, "Pissed");
    if (state->selected_sound_count >= 3 && pissed_count) {
        pissed_index = state->selected_sound_count - 3;
        if (pissed_index >= pissed_count) {
            state->selected_sound_count = 0;
        } else {
            sound = G_UnitAckSoundVariantIndex(label, "Pissed", pissed_index);
        }
    }
    if (!sound && ent->sound.num_select)
        sound = G_RandomResponseSound(ent, ent->sound.select, ent->sound.num_select);
    if (sound && G_QueueUnitResponseSound(ent, sound))
        unit_responses->generation = state->generation;
}

void G_QueueAttackOrderSound(edict_t *ent) {
    cstring_t label;
    uint32_t count;
    int sound;

    if (!ent) return;
    G_ResetSelectionResponseForUnit(ent);
    label = ent->data.UnitUI ? ent->data.UnitUI->soundLabel : NULL;
    count = G_UnitAckSoundVariantCount(label, "YesAttack");
    sound = 0;
    if (count) for (int tries = 0; tries < 11; tries++) {
        sound = G_UnitAckSoundVariantIndex(label, "YesAttack", (uint32_t)(rand() % count));
        if (count == 1 || !G_SoundVariantIsLast(sound, ent->s.player)) break;
    }
    if (!sound && ent->sound.num_yes) sound = G_RandomResponseSound(ent, ent->sound.yes, ent->sound.num_yes);
    if (sound) G_QueueUnitResponseSound(ent, sound);
}

static void G_QueueOrderSound(edict_t *ent) {
    int sound;
    if (!ent) return;
    if (ent->currentmove && ent->currentmove->proc == CAbilityAttack) {
        G_QueueAttackOrderSound(ent);
        return;
    }
    G_ResetSelectionResponseForUnit(ent);
    if (!ent->sound.num_yes) return;
    sound = G_RandomResponseSound(ent, ent->sound.yes, ent->sound.num_yes);
    G_QueueUnitResponseSound(ent, sound);
}

/* select/point are left-click completion paths for targeted commands.  A
 * right-click Smart action cancels that mode instead of being interpreted as
 * a new order by the units that were selected when targeting began. */
static bool G_CancelTargetMode(edict_t *clent) {
    gameClient_t *client = clent ? clent->client : NULL;

    if (!client || (!client->menu.on_entity_selected && !client->menu.on_location_selected))
        return false;
    memset(&client->menu, 0, sizeof(client->menu));
    Get_Commands_f(clent);
    return true;
}

static void G_PrepareUnitShortcut(edict_t *clent) {
    if (!clent || !clent->client) return;
    G_CancelBuildPlacement(clent);
    G_CancelTargetMode(clent);
}

CLIENTCOMMAND(HeroButton) {
    if (argc < 2) return;
    G_PrepareUnitShortcut(clent);
    G_ActivateHeroButton(clent, (uint32_t)atoi(argv[1]));
}

CLIENTCOMMAND(HeroKey) {
    if (argc < 2) return;
    G_PrepareUnitShortcut(clent);
    G_ActivateHeroKey(clent, (uint32_t)atoi(argv[1]));
}

CLIENTCOMMAND(IdleWorker) {
    uint32_t hinted_number = argc >= 2 ? (uint32_t)atoi(argv[1]) : 0;
    G_PrepareUnitShortcut(clent);
    G_ActivateIdleWorkerShortcut(clent, hinted_number);
}

void CMD_CancelCommand(edict_t *ent) {
    edict_t *producer;
    if (ent && ent->client && ent->client->ps.client_ui_state == CLIENT_UI_CINEMATIC) {
        /* Escape skips the cinematic before it can cancel unrelated gameplay work. */
        G_PublishEndCinematicForHumans(ent, false);
        return;
    }
    if (ent && ent->client && (producer = G_GetMainSelectedUnit(ent->client)) &&
        G_UnitCanControl(ent->client, producer)) {
        /* In-place upgrades and spawned construction are both cancelled by
         * the selected structure itself. Keep them ahead of queue cancellation
         * so CmdCancelBuild cannot fall through to unrelated producer state. */
        if (G_BuildingUpgradeActive(producer) && G_CancelBuildingUpgrade(producer)) {
            if (ent->client->connected) {
                G_RefreshResourceBar(ent);
                Get_Portrait_f(ent);
                Get_Commands_f(ent);
            }
            return;
        }
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

static bool G_SelectionMembershipUnchanged(gameClient_t *client, edict_t *const *old_selection, uint32_t old_count) {
    uint32_t current_count = 0;

    if (!client) return false;
    FOR_SELECTED_UNITS(client, selected) {
        bool found = false;
        current_count++;
        FOR_LOOP(i, old_count) {
            if (old_selection[i] == selected) { found = true; break; }
        }
        if (!found) return false;
    }
    return current_count == old_count;
}

CLIENTCOMMAND(Select) {
    gameClient_t *client = clent->client;
    if (client->menu.on_entity_selected) {
        uint32_t number;
        bool const queued = client->menu.supports_order_queue &&
                            G_CommandQueueRequested(argc, argv, 2);
        bool accepted;

        if (argc < 2 || !G_ParseEntityNumber(argv[1], &number)) return;
        client->menu.order_queued = queued;
        accepted = client->menu.on_entity_selected(clent, &globals.edicts[number]);
        client->menu.order_queued = false;
        /* Warsmash keeps a target command armed while Shift is held so the
         * player can click several waypoints/targets without reopening it. */
        if (accepted && !queued) Get_Commands_f(clent);
    } else {
#ifdef WC3_DEBUG_MINING
        if (client->menu.on_location_selected) {
            fprintf(stderr, "WC3_MINING select-while-building client=%ld building=%.4s callback=%p argc=%d",
                    (long)(clent - globals.edicts), (cstring_t)&clent->build_project,
                    (void *)client->menu.on_location_selected, argc);
            if (argc >= 2) {
                uint32_t clicked;
                if (G_ParseEntityNumber(argv[1], &clicked) && clicked < globals.num_edicts)
                    fprintf(stderr, " clicked=%u id=%.4s building=%d player=%u", (unsigned)clicked,
                            (cstring_t)&globals.edicts[clicked].class_id,
                            G_UnitIsBuilding(globals.edicts[clicked].class_id),
                            (unsigned)globals.edicts[clicked].s.player);
            }
            fputc('\n', stderr);
        }
#endif
        if (client->menu.on_location_selected == build_menu_send_builder && clent->build_project && argc >= 2) {
            uint32_t number;
            if (G_ParseEntityNumber(argv[1], &number) && number < globals.num_edicts) {
                edict_t *target = &globals.edicts[number];
                UnitData_t const *building_data = G_UnitData(clent->build_project);
                if (building_data && building_data->isBuildOn && S_GoldMineIsMine(target)) {
                    bool const queued = client->menu.supports_order_queue &&
                                        G_CommandQueueRequested(argc, argv, 2);
                    bool accepted;
#ifdef WC3_DEBUG_MINING
                    fprintf(stderr, "WC3_MINING select-route-to-build client=%ld building=%.4s mine=%ld mine_id=%.4s point=(%.1f,%.1f)\n",
                            (long)(clent - globals.edicts), (cstring_t)&clent->build_project,
                            (long)(target - globals.edicts), (cstring_t)&target->class_id,
                            target->s.origin2.x, target->s.origin2.y);
#endif
                    client->menu.order_queued = queued;
                    accepted = build_menu_send_builder(clent, &target->s.origin2);
                    client->menu.order_queued = false;
                    if (accepted && !queued) Get_Commands_f(clent);
                    return;
                }
            }
            /* Any ordinary selection click replaces building placement. Invalid
             * terrain placement is handled by Point and intentionally stays armed. */
            G_CancelBuildPlacement(clent);
        }
        bool cleared = false;
        bool hasunits = false;
        bool const same_type = argc >= 3 && !strcmp(argv[2], "sametype");
        edict_t *same_type_anchor = NULL;
        edict_t *voice = NULL;
        edict_t *old_selection[WC3_SELECTION_LIMIT] = { 0 };
        uint32_t old_count = 0;
        uint32_t selected_count = 0;

        FOR_SELECTED_UNITS(client, selected) {
            if (old_count >= WC3_SELECTION_LIMIT) break;
            old_selection[old_count++] = selected;
        }
        if (same_type) {
            uint32_t anchor_number;
            if (!G_ParseEntityNumber(argv[1], &anchor_number)) return;
            same_type_anchor = &globals.edicts[anchor_number];
            if (!G_UnitCanBeSelected(client, same_type_anchor)) return;
        }
        for (uint32_t i = 1; i < argc; i++) {
            uint32_t number;
            if (!G_ParseEntityNumber(argv[i], &number)) continue;
            edict_t *e = &globals.edicts[number];
            if (same_type && e->class_id != same_type_anchor->class_id) continue;
            if (G_UnitCanBeSelected(client, e) && G_UnitCanControl(client, e) &&
                !G_UnitIsBuilding(e->class_id)) {
                hasunits = true;
            }
        }
        for (uint32_t i = 1; i < argc; i++) {
            uint32_t number;
            if (!G_ParseEntityNumber(argv[i], &number)) continue;
            edict_t *e = &globals.edicts[number];
            if (same_type && e->class_id != same_type_anchor->class_id) continue;
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
            edict_t *ordered[1];

            /* Warsmash chooses the first unit after its priority/level/rawcode
             * sort as the primary selection.  Use the same unit for default
             * focus and the initial selection acknowledgement. */
            if (G_GetOrderedSelectedUnits(client, ordered, sizeof(ordered) / sizeof(ordered[0]))) {
                G_FocusSelectedUnit(client, ordered[0]);
                voice = ordered[0];
            } else {
                voice = NULL;
            }
            if (G_UnitCanControl(client, voice)) {
                G_QueueSelectionSound(voice, G_SelectionMembershipUnchanged(client, old_selection, old_count) ? false : true);
            } else if (voice && voice->s.player != PLAYER_NEUTRAL_PASSIVE) {
                /* Ordinary foreign units use interface feedback rather than
                 * speaking their owner's selection acknowledgement. Neutral
                 * Passive critter response rules remain a separate gap. */
                G_PlayUISoundForPlayer(clent, "InterfaceClick");
            }
            /* The client sends complete selection membership. Publish JASS
             * selection events from the final authoritative delta instead of
             * while the list is temporarily cleared/rebuilt, which would emit
             * false deselect/select pairs for unchanged members. */
            G_PublishSelectionDelta(client, old_selection, old_count);

            /* Selection is authoritative game state. Mirror the accepted,
             * server-filtered membership back to the client cache as well as
             * rebuilding the HUD so the client cannot retain current-selection entries
             * that the server rejected. */
            G_SyncClientSelection(client);
        }
    }
}

void G_SendPointConfirmation(edict_t *clent, vector2_t const *point, bool attack) {
    if (!clent || !clent->client || !point) return;
    gi.Write(PF_BYTE, &(int32_t){ svc_temp_entity });
    gi.Write(PF_BYTE, &(int32_t){ attack ? TE_ATTACK_CONFIRMATION : TE_MOVE_CONFIRMATION });
    gi.Write(PF_POSITION, &(vector3_t){ point->x, point->y, 0 });
    gi.unicast(clent);
}

CLIENTCOMMAND(CycleSubgroup) {
    gameClient_t *client = clent ? clent->client : NULL;

    if (!client || G_TargetModeActive(client)) return;
    if (!G_CycleSelectionSubgroup(client)) return;

    /* Match portrait-click focus changes: selection membership is unchanged,
     * but every focused-subgroup presentation consumer must move together. */
    Get_Portrait_f(clent);
    Get_Commands_f(clent);
    G_PlayUISoundForPlayer(clent, "SubGroupSelectionChange");
}

CLIENTCOMMAND(Focus) {
    gameClient_t *client = clent ? clent->client : NULL;
    uint32_t number;
    edict_t *target;

    if (!client || argc < 2) return;
    number = (uint32_t)atoi(argv[1]);
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

    /* Warsmash gives the already-focused multiselect portrait a second-click
     * meaning: collapse the authoritative selection to that exact unit. A
     * different portrait, including another unit in the same type subgroup,
     * only changes focus and leaves the full selection intact. */
    if (G_GetMainSelectedUnit(client) == target) {
        edict_t *old_selection[WC3_SELECTION_LIMIT] = { 0 };
        uint32_t old_count = 0;

        FOR_SELECTED_UNITS(client, selected) {
            if (old_count >= WC3_SELECTION_LIMIT) break;
            old_selection[old_count++] = selected;
        }
        if (old_count <= 1) return;

        FOR_LOOP(i, old_count) {
            if (old_selection[i] != target) G_DeselectEntity(client, old_selection[i]);
        }
        G_FocusSelectedUnit(client, target);

        if (G_UnitCanControl(client, target)) {
            G_QueueSelectionSound(target, true);
        } else if (target->s.player != PLAYER_NEUTRAL_PASSIVE) {
            G_PlayUISoundForPlayer(clent, "InterfaceClick");
        }
        G_PublishSelectionDelta(client, old_selection, old_count);
        G_SyncClientSelection(client);
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
    gameClient_t *client = clent->client;
    if (argc < 3) return;
    if (client->menu.on_location_selected) {
        bool const queued = client->menu.supports_order_queue &&
                            G_CommandQueueRequested(argc, argv, 3);
        vector2_t loc = { atoi(argv[1]), atoi(argv[2]) };
        bool accepted;

        client->menu.order_queued = queued;
        accepted = client->menu.on_location_selected(clent, &loc);
        client->menu.order_queued = false;
        if (accepted && !queued) Get_Commands_f(clent);
    }
}

CLIENTCOMMAND(OrderQueueRelease) {
    gameClient_t *client = clent ? clent->client : NULL;

    (void)argc;
    (void)argv;
    if (!client || !client->menu.order_queue_chained) return;
    if (client->menu.on_location_selected == build_menu_send_builder) {
        G_CancelBuildPlacement(clent);
    }
}

CLIENTCOMMAND(Smart) {
    gameClient_t *client = clent->client;
    bool issued = false;
    bool rallied = false;
    bool queued;
    bool have_click_point = false;
    vector2_t click_point = { 0 };
    uint32_t number;
    edict_t *target;

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
    if (argc >= 4 && strcmp(argv[2], "queue") && strcmp(argv[3], "queue")) {
        click_point = (vector2_t){ atoi(argv[2]), atoi(argv[3]) };
        have_click_point = true;
    }
    FOR_CONTROLLABLE_SELECTED_UNITS(client, ent) {
        /* A queued Smart target is otherwise accepted into the FIFO before
         * its destructable semantics are evaluated, preventing the clicked
         * walkable surface from falling back to the queued ground move. */
        if (queued && have_click_point && G_DestructableIsWalkable(target) &&
            !G_DestructableAcceptsSmartAttack(ent, target)) continue;
        if (G_IssueUnitTargetOrder(ent, "smart", target, queued, client->ps.number)) {
            if (G_UnitHasRally(ent)) rallied = true;
            issued = true;
        }
    }
    /* A live walkable destructable is also traversable ground. If its entity
     * Smart action was rejected, preserve the world point under the cursor and
     * run the normal formation-aware ground Smart path instead. Keep the
     * gameplay decision on destructable state rather than presentation flags. */
    if (!issued && have_click_point && G_DestructableIsWalkable(target)) {
        bool const old_queued = client->menu.order_queued;
        client->menu.order_queued = queued;
        issued = move_selectlocation(clent, &click_point);
        client->menu.order_queued = old_queued;
    }
    if (issued) {
        /* Retail briefly flashes the clicked unit's selection ring after a
         * successful Smart target order. The relationship colour is local to
         * the issuing player and the existing indicator event owns timing. */
        if (target->svflags & SVF_MONSTER)
            G_SendWidgetIndicator(target, G_SmartTargetIndicatorColor(client->ps.number, target), &client->ps);
        G_QueueOrderSound(G_GetMainControllableUnit(client));
        if (rallied) G_PlayUISoundForPlayer(clent, "RallyPointPlace");
        Get_Commands_f(clent);
    }
}

CLIENTCOMMAND(SmartPoint) {
    gameClient_t *client = clent->client;
    vector2_t loc;
    bool rally = false;
    bool non_rally = false;
    bool issued = false;
    bool queued;

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
    loc = (vector2_t){ atoi(argv[1]), atoi(argv[2]) };
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
        bool const old_queued = client->menu.order_queued;
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
    cstring_t classname;
    gameClient_t *client = clent->client;
    ability_t const *ability;
    edict_t *producer;
    bool ability_off = false;

    if (argc < 2) return;
    producer = G_GetMainSelectedUnit(client);
    classname = argv[1];
    /* A neutral shop remains neutral selection state; buying from it must not
     * weaken G_UnitCanControl() for ordinary enemy/neutral units. Merchandise
     * commands are raw object IDs and the authoritative stock path decides
     * whether the selected shop sells an item or a non-Hero unit. */
    if (G_CanUseItemShop(client, producer) || G_CanUseUnitShop(client, producer)) {
        uint32_t merchandise_id = 0;
        if (strlen(classname) != 4) return;
        memcpy(&merchandise_id, classname, sizeof(merchandise_id));
        if (G_ShopSellsItem(producer, merchandise_id)) {
            if (G_ShopPurchaseItem(clent, producer, merchandise_id)) Get_Portrait_f(clent);
        } else if (G_ShopSellsUnit(producer, merchandise_id)) {
            G_ShopPurchaseUnit(clent, producer, merchandise_id);
        }
        Get_Commands_f(clent);
        return;
    }
    if (!G_UnitCanControl(client, producer)) return;
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
    if (S_AbilityHasCommand(ability)) {
        client->menu.ability_code = *((uint32_t const *)classname);
        client->menu.ability_off = ability_off;
        S_AbilityCommand(clent, ability);
        client->menu.ability_off = false;
    } else if (client->menu.cmdbutton) {
        client->menu.cmdbutton(clent, *((uint32_t *)classname));
    } else {
        uint32_t class_id = 0;

        if (strlen(classname) != 4) return;
        memcpy(&class_id, classname, sizeof(class_id));
        SP_TrainUnit(producer, class_id);
    }
}

CLIENTCOMMAND(Autocast) {
    gameClient_t *client;
    edict_t *main;
    ability_t const *ability;
    cstring_t classname;
    bool enabled;
    bool changed = false;

    if (!clent || !clent->client || argc < 2) return;
    client = clent->client;
    classname = argv[1];
    if (!classname || strlen(classname) != 4) return;
    main = G_GetMainSelectedUnit(client);
    ability = FindAbilityForCommand(classname);
    if (!G_UnitCanControl(client, main) || !G_ActorHasSkill(main, classname) ||
        !ability || !(ability->flags & AB_AUTOCAST)) {
#ifdef WC3_DEBUG_AUTOCAST
        if (G_AutocastDebugLevel() >= 1) {
            fprintf(stderr,
                    "WC3_AUTOCAST command_rejected unit=%ld code=%s control=%d has_skill=%d ability=%p set=%d state=%d\n",
                    main && g_edicts ? (long)(main - g_edicts) : -1L,
                    classname,
                    G_UnitCanControl(client, main) ? 1 : 0,
                    main && G_ActorHasSkill(main, classname) ? 1 : 0,
                    (void *)ability,
                    ability && (ability->flags & AB_AUTOCAST) ? 1 : 0,
                    ability && (ability->flags & AB_AUTOCAST) ? 1 : 0);
        }
#endif
        return;
    }

    enabled = !G_UnitAutocastIsOn(main, FS_SLKKey(classname));
    FOR_CONTROLLABLE_SELECTED_UNITS(client, ent) {
        if (!G_ActorHasSkill(ent, classname)) continue;
        if (G_SetUnitAutocast(ent, FS_SLKKey(classname), enabled)) {
            changed = true;
            /* The toggle itself must not interrupt active work or movement, but
             * an already-idle worker should respond immediately rather than
             * waiting for the next staggered 300 ms acquisition slot. */
            if (enabled && G_UnitIsIdleWorker(ent)) {
#ifdef WC3_DEBUG_AUTOCAST
                bool const acquired = G_TryUnitAutocast(ent);
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
    cstring_t classname = argc >= 2 ? argv[1] : NULL;
    gameClient_t *client = clent->client;
    edict_t *ent = G_GetMainSelectedUnit(client);
    uint32_t abilcode = 0;

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

CLIENTCOMMAND(Upgrade) {
    cstring_t classname = argc >= 2 ? argv[1] : NULL;
    gameClient_t *client = clent ? clent->client : NULL;
    edict_t *ent = client ? G_GetMainSelectedUnit(client) : NULL;
    uint32_t unit_id = 0;

    if (!G_UnitCanControl(client, ent) || !classname || strlen(classname) != 4) return;
    memcpy(&unit_id, classname, sizeof(unit_id));
    G_StartBuildingUpgrade(ent, unit_id);
    Get_Commands_f(clent);
}

bool G_CheatsEnabled(void) {
    return atoi(gi.CvarString("sv_cheats", "0")) != 0;
}

static edict_t *G_GiveItem(edict_t *unit, uint32_t item_code) {
    edict_t *item = SP_SpawnAtLocation(item_code, unit->s.player, &unit->s.origin2);
    if (!item || !G_PickupItem(unit, item)) {
        if (item) G_RemoveItem(item);
        return NULL;
    }
    return item;
}

static bool G_ParseGiveResourceAmount(cstring_t text, uint32_t *amount) {
    unsigned long value;

    if (!amount || !G_DebugIsNumber(text) || text[0] == '-') return false;
    value = strtoul(text, NULL, 10);
    *amount = (uint32_t)MIN(value, (unsigned long)USHRT_MAX);
    return true;
}

static void G_AddGiveResource(gameClient_t *client, uint32_t state, uint32_t amount) {
    uint32_t value;

    if (!client || state >= MAX_STATS) return;
    value = client->ps.stats[state];
    client->ps.stats[state] = (uint16_t)MIN(value + amount, (uint32_t)USHRT_MAX);
}

static bool G_GivePlayerResources(edict_t *clent, cstring_t target, cstring_t value) {
    gameClient_t *client = clent ? clent->client : NULL;
    uint32_t amount;
    bool const give_gold = !strcasecmp(target, "gold") || !strcasecmp(target, "res");
    bool const give_lumber = !strcasecmp(target, "lumber") || !strcasecmp(target, "res");

    if (!give_gold && !give_lumber) return false;
    if (!G_ParseGiveResourceAmount(value, &amount)) {
        G_CheatPrintf(clent, "WC3: resource amount must be a non-negative integer");
        return true;
    }
    if (!client) return true;
    if (give_gold) G_AddGiveResource(client, PLAYERSTATE_RESOURCE_GOLD, amount);
    if (give_lumber) G_AddGiveResource(client, PLAYERSTATE_RESOURCE_LUMBER, amount);
    G_InvalidateCommands(client);
    G_CheatPrintf(clent, "WC3: gave %u %s to player %u",
            (unsigned)amount,
            give_gold && give_lumber ? "gold and lumber" : give_gold ? "gold" : "lumber",
            (unsigned)client->ps.number);
    return true;
}

CLIENTCOMMAND(Give) {
    gameClient_t *client = clent->client;
    edict_t *unit;
    uint32_t code;

    if (!G_CheatsEnabled()) {
        G_CheatPrintf(clent, "WC3: cheats are disabled; set sv_cheats 1");
        return;
    }
    if (argc < 2) {
        G_CheatPrintf(clent,
            "WC3: cheats: give gold <amount> | lumber <amount> | res <amount> | "
            "item <rawcode> [count] | ability <rawcode> | xp <amount>");
        return;
    }
    if (argc < 3) {
        G_CheatPrintf(clent, "WC3: give requires a target and value");
        return;
    }
    if (G_GivePlayerResources(clent, argv[1], argv[2])) return;

    unit = G_GetMainSelectedUnit(client);
    if (!unit) {
        G_CheatPrintf(clent, "WC3: give %s requires a selected unit", argv[1]);
        return;
    }
    if (strcasecmp(argv[1], "xp") && strlen(argv[2]) < 4) {
        G_CheatPrintf(clent, "WC3: rawcode must contain four characters");
        return;
    }
    if (!strcasecmp(argv[1], "item")) {
        code = *(uint32_t const *)argv[2];
        if (!G_GiveItem(unit, code)) {
            G_CheatPrintf(clent, "WC3: could not give item %.4s to selected unit", argv[2]);
            return;
        }
        G_CheatPrintf(clent, "WC3: gave item %.4s to selected unit", argv[2]);
    } else if (!strcasecmp(argv[1], "ability")) {
        code = *(uint32_t const *)argv[2];
        unit_learnability(unit, code);
        G_CheatPrintf(clent, "WC3: gave ability %.4s to selected unit", argv[2]);
    } else if (!strcasecmp(argv[1], "xp")) {
        if (!G_UnitIsHero(unit)) {
            G_CheatPrintf(clent, "WC3: selected unit is not a hero");
            return;
        }
        G_HeroSetXP(unit, unit->hero.xp + (uint32_t)strtoul(argv[2], NULL, 10));
        G_CheatPrintf(clent, "WC3: gave %s XP to selected hero", argv[2]);
    } else {
        G_CheatPrintf(clent, "WC3: unsupported give target '%s'", argv[1]);
        return;
    }
    Get_Commands_f(clent);
    Get_Portrait_f(clent);
}

/* RTS controllers are not actors: unit cheats act on the primary controllable selection. */
CLIENTCOMMAND(God) {
    edict_t *unit = G_GetMainControllableUnit(clent->client);
    if (!G_CheatsEnabled()) {
        G_CheatPrintf(clent, "WC3: cheats are disabled; set sv_cheats 1");
        return;
    }
    if (!unit) { G_CheatPrintf(clent, "WC3: god requires a selected controllable unit"); return; }
    /* The former controller flag could never protect the selected actor in T_Damage. */
    unit->invulnerable = !unit->invulnerable;
    G_CheatPrintf(clent, "WC3: god %s", unit->invulnerable ? "on" : "off");
}

/* Suicide must execute the same death callback as combat, including food, selection and trigger cleanup. */
CLIENTCOMMAND(Kill) {
    edict_t *unit = G_GetMainControllableUnit(clent->client);
    if (!G_CheatsEnabled()) {
        G_CheatPrintf(clent, "WC3: cheats are disabled; set sv_cheats 1");
        return;
    }
    if (!unit) { G_CheatPrintf(clent, "WC3: kill requires a selected controllable unit"); return; }
    if (!unit->die) { G_CheatPrintf(clent, "WC3: selected unit %u has no death callback", unit->s.number); return; }
    /* Q2 clears godmode before suicide; setting controller health alone skipped every actor death effect. */
    unit->invulnerable = false;
    G_SetHealth(unit, 0);
    unit->die(unit, unit);
    G_CheatPrintf(clent, "WC3: selected unit killed");
}

/* Parse a non-negative Hero stat amount and clamp it to the authored maximum. */
static bool G_ParseHeroStatAmount(cstring_t text, float maximum, float *value) {
    unsigned long amount;

    if (!value || !text || !G_DebugIsNumber(text) || text[0] == '-') return false;
    amount = strtoul(text, NULL, 10);
    *value = MIN((float)amount, MAX(0.0f, maximum));
    return true;
}

/* Apply the selected controllable Hero cheat without bypassing normal state ownership. */
CLIENTCOMMAND(Hero) {
    gameClient_t *client = clent ? clent->client : NULL;
    edict_t *hero;
    char hero_number[16];
    uint32_t max_level, spent_points = 0, expected_points;
    float value;

    if (!G_CheatsEnabled()) {
        G_CheatPrintf(clent, "WC3: cheats are disabled; set sv_cheats 1");
        return;
    }
    if (argc == 2 && !strcasecmp(argv[1], "select")) {
        if (!client) {
            G_CheatPrintf(clent, "WC3: hero select requires a player client");
            return;
        }
        FOR_LOOP(i, globals.num_edicts) {
            edict_t *candidate = &globals.edicts[i];
            if (!candidate->inuse || !(candidate->svflags & SVF_MONSTER) ||
                    candidate->s.player != client->ps.number || !G_UnitIsHero(candidate) ||
                    !G_UnitCanBeSelected(client, candidate) || !G_UnitCanControl(client, candidate)) continue;
            snprintf(hero_number, sizeof(hero_number), "%u", (unsigned)candidate->s.number);
            {
                cstring_t select[] = { "select", hero_number };
                G_PrepareUnitShortcut(clent);
                CMD_Select(clent, 2, select);
            }
            G_CheatPrintf(clent, "WC3: selected hero unit %u", (unsigned)candidate->s.number);
            return;
        }
        G_CheatPrintf(clent, "WC3: no controllable hero found for player %u", (unsigned)client->ps.number);
        return;
    }
    if (argc >= 2 && !strcasecmp(argv[1], "dump")) {
        char snap[512];
        hero = client ? G_GetMainSelectedUnit(client) : NULL;
        if (!hero || !G_UnitIsHero(hero)) {
            FOR_LOOP(i, globals.num_edicts) {
                edict_t *candidate = &globals.edicts[i];
                if (candidate->inuse && (candidate->svflags & SVF_MONSTER) && !M_IsDead(candidate) &&
                        G_UnitIsHero(candidate) && (!client || candidate->s.player == client->ps.number)) {
                    hero = candidate;
                    break;
                }
            }
        }
        G_FormatHeroSaveSnap(hero, snap, sizeof(snap));
        G_CheatPrintf(clent, "HERO_SAVELOAD snapshot=dump %s", snap);
        return;
    }
    if (argc >= 2 && !strcasecmp(argv[1], "walk")) {
        vector2_t dest;
        float dx = 80.0f, dy = 0.0f;
        hero = client ? G_GetMainSelectedUnit(client) : NULL;
        if (!hero || !G_UnitCanControl(client, hero) || !G_UnitIsHero(hero)) {
            G_CheatPrintf(clent, "WC3: hero walk requires a selected friendly hero");
            return;
        }
        if (argc == 4 && G_DebugIsNumber(argv[2]) && G_DebugIsNumber(argv[3])) {
            dx = (float)atoi(argv[2]);
            dy = (float)atoi(argv[3]);
        } else if (argc != 2) {
            G_CheatPrintf(clent, "WC3: usage: hero walk [dx dy]");
            return;
        }
        dest = (vector2_t){ hero->s.origin2.x + dx, hero->s.origin2.y + dy };
        if (!unit_issueorder(hero, "move", &dest)) {
            G_CheatPrintf(clent, "WC3: hero walk order rejected");
            return;
        }
        G_CheatPrintf(clent, "WC3: hero walk to %.0f %.0f", dest.x, dest.y);
        return;
    }
    if (argc < 2 || argc > 3 ||
            (strcasecmp(argv[1], "max") && strcasecmp(argv[1], "health") && strcasecmp(argv[1], "mana")) ||
            (!strcasecmp(argv[1], "max") && argc != 2)) {
        G_CheatPrintf(clent, "WC3: usage: hero select | hero dump | hero walk [dx dy] | hero max | hero health [amount] | hero mana [amount]");
        return;
    }
    hero = client ? G_GetMainSelectedUnit(client) : NULL;
    if (!hero || !G_UnitCanControl(client, hero) || !G_UnitIsHero(hero)) {
        G_CheatPrintf(clent, "WC3: hero cheat requires a selected friendly hero");
        return;
    }

    if (!strcasecmp(argv[1], "health")) {
        value = hero->health.max_value;
        if (argc == 3 && !G_ParseHeroStatAmount(argv[2], hero->health.max_value, &value)) {
            G_CheatPrintf(clent, "WC3: hero health amount must be a non-negative integer");
            return;
        }
        G_SetHealth(hero, value);
        G_CheatPrintf(clent, "WC3: selected hero health set to %.0f / %.0f",
                hero->health.value, hero->health.max_value);
        G_InvalidateUnitInfoPanel(hero);
        return;
    }

    if (!strcasecmp(argv[1], "mana")) {
        value = hero->mana.max_value;
        if (argc == 3 && !G_ParseHeroStatAmount(argv[2], hero->mana.max_value, &value)) {
            G_CheatPrintf(clent, "WC3: hero mana amount must be a non-negative integer");
            return;
        }
        hero->mana.value = value;
        G_CheatPrintf(clent, "WC3: selected hero mana set to %.0f / %.0f",
                hero->mana.value, hero->mana.max_value);
        G_InvalidateUnitInfoPanel(hero);
        return;
    }

    max_level = G_MaxHeroLevel();
    G_HeroSetXP(hero, G_HeroXPForLevel(max_level));
    FOR_LOOP(i, MAX_HERO_ABILITIES) spent_points += hero->heroabilities[i].level;
    expected_points = max_level > spent_points ? max_level - spent_points : 0;
    if (hero->hero.skillpoints < expected_points) {
        G_HeroModifySkillPoints(hero, (int32_t)(expected_points - hero->hero.skillpoints));
    }

    G_CheatPrintf(clent, "WC3: selected hero set to level %u with %u skill points",
            (unsigned)hero->hero.level, (unsigned)hero->hero.skillpoints);
    G_InvalidateCommands(client);
    G_InvalidateUnitInfoPanel(hero);
}

/* Keep the instant-build cheat scoped to the issuing player's live client state. */
bool G_PlayerInstantBuild(uint32_t player) {
    gameClient_t *client = G_GetPlayerClientByNumber(player);
    return client && client->ps.number == player && client->cheat_instant_build;
}

/* Keep one-hit damage scoped to the issuing player's live client state. */
bool G_PlayerInstantKill(uint32_t player) {
    gameClient_t *client = G_GetPlayerClientByNumber(player);
    return client && client->ps.number == player && client->cheat_instant_kill;
}

/* Parse the toggle spellings shared by developer cheats and their aliases. */
static bool G_ParseCheatToggle(cstring_t value, bool current, bool *out) {
    if (!out) return false;
    if (!value || !*value) {
        *out = !current;
        return true;
    }
    FOR_LOOP(i, sizeof(cheat_toggle_values) / sizeof(*cheat_toggle_values))
        if (!strcasecmp(value, cheat_toggle_values[i].name)) {
            *out = cheat_toggle_values[i].value;
            return true;
        }
    return false;
}

/* Parse and apply the issuing player's instant-build toggle. */
static bool G_CheatInstantBuild(edict_t *clent, cstring_t value, cstring_t usage) {
    gameClient_t *client = clent ? clent->client : NULL;
    bool enabled;

    if (!G_CheatsEnabled()) {
        G_CheatPrintf(clent, "WC3: cheats are disabled; set sv_cheats 1");
        return false;
    }
    if (!client) return false;
    if (!G_ParseCheatToggle(value, client->cheat_instant_build, &enabled)) {
        G_CheatPrintf(clent, "WC3: usage: %s [on|off]", usage);
        return false;
    }
    client->cheat_instant_build = enabled;
    G_CheatPrintf(clent, "WC3: instant build %s for player %u",
            enabled ? "on" : "off", (unsigned)client->ps.number);
    return true;
}

/* Parse and apply the issuing player's instant-kill toggle. */
static bool G_CheatInstantKill(edict_t *clent, cstring_t value) {
    gameClient_t *client = clent ? clent->client : NULL;
    bool enabled;

    if (!G_CheatsEnabled()) {
        G_CheatPrintf(clent, "WC3: cheats are disabled; set sv_cheats 1");
        return false;
    }
    if (!client) return false;
    if (!G_ParseCheatToggle(value, client->cheat_instant_kill, &enabled)) {
        G_CheatPrintf(clent, "WC3: usage: instant kill [on|off]");
        return false;
    }
    client->cheat_instant_kill = enabled;
    G_CheatPrintf(clent, "WC3: instant kill %s for player %u",
            enabled ? "on" : "off", (unsigned)client->ps.number);
    return true;
}

/* Warcraft-style single-token alias retained for instant build. */
CLIENTCOMMAND(InstantBuild) {
    if (argc > 2) {
        G_CheatPrintf(clent, "WC3: usage: warpten [on|off]");
        return;
    }
    G_CheatInstantBuild(clent, argc == 2 ? argv[1] : NULL, "warpten");
}

/* Dispatch the grouped instant cheats while keeping their player-local state independent. */
CLIENTCOMMAND(Instant) {
    gameClient_t *client = clent ? clent->client : NULL;
    bool enabled;

    if (!G_CheatsEnabled()) {
        G_CheatPrintf(clent, "WC3: cheats are disabled; set sv_cheats 1");
        return;
    }
    if (!client) return;
    if (argc < 2 || argc > 3) {
        G_CheatPrintf(clent, "WC3: usage: instant <build|kill|all> [on|off]");
        return;
    }
    if (!strcasecmp(argv[1], "build")) {
        G_CheatInstantBuild(clent, argc == 3 ? argv[2] : NULL, "instant build");
        return;
    }
    if (!strcasecmp(argv[1], "kill")) {
        G_CheatInstantKill(clent, argc == 3 ? argv[2] : NULL);
        return;
    }
    if (!strcasecmp(argv[1], "all")) {
        bool const all_enabled = client->cheat_instant_build && client->cheat_instant_kill;
        if (!G_ParseCheatToggle(argc == 3 ? argv[2] : NULL, all_enabled, &enabled)) {
            G_CheatPrintf(clent, "WC3: usage: instant all [on|off]");
            return;
        }
        client->cheat_instant_build = enabled;
        client->cheat_instant_kill = enabled;
        G_CheatPrintf(clent, "WC3: instant all %s for player %u",
                enabled ? "on" : "off", (unsigned)client->ps.number);
        return;
    }
    G_CheatPrintf(clent, "WC3: usage: instant <build|kill|all> [on|off]");
}

static void G_CheatGameResult(edict_t *clent, uint32_t game_result) {
    if (!G_CheatsEnabled()) {
        G_CheatPrintf(clent, "WC3: cheats are disabled; set sv_cheats 1");
        return;
    }
    if (!clent || !clent->client) return;
    G_CheatPrintf(clent, "WC3: %s cheat applied for player %u",
            game_result == 0 ? "win" : "lose", (unsigned)clent->client->ps.number);
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

static float G_CheatTimeOfDayTarget(bool daytime) {
    float const day_hours = game.constants.gameDayHours;
    float const dawn = game.constants.dawnTimeGameHours;
    float const dusk = game.constants.duskTimeGameHours;
    float target;

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

static void G_CheatSetTimeOfDay(edict_t *clent, bool daytime) {
    if (!G_CheatsEnabled()) {
        G_CheatPrintf(clent, "WC3: cheats are disabled; set sv_cheats 1");
        return;
    }
    G_SetTimeOfDay(G_CheatTimeOfDayTarget(daytime));
    G_CheatPrintf(clent, "WC3: time of day set to %s", daytime ? "day" : "night");
}

CLIENTCOMMAND(Day) {
    (void)argc; (void)argv;
    G_CheatSetTimeOfDay(clent, true);
}

CLIENTCOMMAND(Night) {
    (void)argc; (void)argv;
    G_CheatSetTimeOfDay(clent, false);
}

static edict_t *G_GetInventoryInteractionUnit(gameClient_t *client) {
    edict_t *selected;

    if (!client) return NULL;
    selected = G_GetMainSelectedUnit(client);
    if (G_CanUseItemShop(client, selected)) return G_FindShopPatron(client, selected);
    return selected;
}

CLIENTCOMMAND(Inventory) {
    gameClient_t *client = clent->client;
    edict_t *ent;
    edict_t *item;
    int32_t slot;
    cstring_t abilities;
    bool handled = false;

    if (argc < 2) {
        return;
    }

    ent = G_GetInventoryInteractionUnit(client);
    slot = atoi(argv[1]);
    if (!G_UnitCanControl(client, ent) || !G_InventoryCanUseItems(ent) ||
        slot < 0 || (uint32_t)slot >= G_InventoryCapacity(ent)) {
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
            abilityitem_t ability_item = MAKE(abilityitem_t, .code = FS_SLKKey(ability_name), .ability = ability);
            abilityCall_t call = MAKE(abilityCall_t, .item = &ability_item, .client = clent);
            bool succeeded = false;

            if (!ability) continue;
            client->menu.ability_code = *((uint32_t const *)ability_name);
            if (ability->flags & AB_ITEM) {
                succeeded = S_AbilityMessage(clent, A_ITEM_USE, &call);
                handled = true;
            } else if (S_AbilityHasCommand(ability)) {
                /* Bind the carried item to the asynchronous spell command.
                 * The target callback completes item use only after A_EXECUTE
                 * succeeds, so selecting/approaching a target does not consume
                 * the charge prematurely. */
                client->menu.ability_item = item;
                client->menu.ability_item_spawn_time = item->spawn_time;
                S_AbilityCommand(clent, ability);
                handled = true;
                if (!client->menu.on_entity_selected && !client->menu.on_location_selected) {
                    client->menu.ability_item = NULL;
                    client->menu.ability_item_spawn_time = 0;
                }
            }

            if (succeeded) G_CompleteItemUse(ent, item);
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

CLIENTCOMMAND(CargoUnload) {
    gameClient_t *client;
    edict_t *transport;
    int32_t slot;

    if (!clent || !(client = clent->client) || argc < 2) return;
    transport = G_GetMainSelectedUnit(client);
    slot = atoi(argv[1]);
    if (!G_UnitCanControl(client, transport) || slot < 0) return;
    if ((uint32_t)slot >= transport->cargo.count) return;
    S_CargoUnloadAt(transport, (uint32_t)slot);
}

CLIENTCOMMAND(CancelTrain) {
    gameClient_t *client;
    edict_t *producer;
    char *end = NULL;
    unsigned long parsed;
    uint32_t index;

    if (!clent || !clent->client || argc < 2 || !argv[1] || !*argv[1]) return;
    parsed = strtoul(argv[1], &end, 10);
    if (!end || *end || parsed > UINT_MAX) return;
    client = clent->client;
    producer = G_GetMainSelectedUnit(client);
    if (!G_UnitCanControl(client, producer) || !producer->build || !producer->build->training) return;
    index = (uint32_t)parsed;
    if (!G_CancelTrainingQueueItem(producer, index, true)) return;
    Get_Portrait_f(clent);
    Get_Commands_f(clent);
}
/* Keep an unsupported entity drop in target mode until the player cancels it. */
static bool G_ItemDragSelectEntity(edict_t *clent, edict_t *target) {
    gameClient_t *client = clent ? clent->client : NULL;
    edict_t *item = client ? client->menu.dragged_item : NULL;
    edict_t *carrier = G_IsItem(item) ? item->item.carrier : NULL;

    if (client && G_CanUseItemShop(client, target) && G_UnitCanControl(client, carrier) &&
        G_InventoryCanDropItems(carrier) && G_ShopPawnItem(&(shopPawnItemParams_t){
            .clent = clent, .shop = target, .carrier = carrier, .item = item })) {
        G_RefreshResourceBar(clent);
        Get_Portrait_f(clent);
        return true;
    }
    /* Warsmash uses an entity-target drop for allied item handoff. OpenRealm
     * does not yet have that transfer behavior, but keep the drag target mode
     * authoritative instead of letting a unit click fall through to selection. */
    return false;
}

/* Complete an inventory point drop using the exact item captured by the drag command. */
static bool G_ItemDragSelectLocation(edict_t *clent, vector2_t const *location) {
    gameClient_t *client = clent ? clent->client : NULL;
    edict_t *unit;
    edict_t *item;

    if (!client || !location) return false;
    item = client->menu.dragged_item;
    unit = G_IsItem(item) ? item->item.carrier : NULL;
    if (!G_UnitCanControl(client, unit) || !G_InventoryCanDropItems(unit) ||
        !G_IsItem(item) || item->item.carrier != unit)
        return false;
    if (!G_OrderDropItemAt(unit, item, location)) return false;
    G_SendPointConfirmation(clent, location, false);
    return true;
}

CLIENTCOMMAND(ItemDrag) {
    gameClient_t *client;
    edict_t *unit;
    edict_t *item;
    int32_t slot;

    if (!clent || !(client = clent->client) || argc < 2) return;
    unit = G_GetInventoryInteractionUnit(client);
    slot = atoi(argv[1]);
    if (!G_UnitCanControl(client, unit) || !G_InventoryCanDropItems(unit) ||
        slot < 0 || (uint32_t)slot >= G_InventoryCapacity(unit)) return;
    item = unit->inventory[slot];
    if (!G_IsItem(item) || item->item.carrier != unit || item->item.in_world) return;

    /* Right-clicking an occupied inventory button enters the same server-owned
     * target-mode lifecycle used by WC3 point abilities. A following left-click
     * on terrain therefore submits a point drop; right-click/CmdCancel clears
     * it through the normal target-mode cancellation path. */
    memset(&client->menu, 0, sizeof(client->menu));
    client->menu.dragged_item = item;
    client->menu.on_entity_selected = G_ItemDragSelectEntity;
    client->menu.on_location_selected = G_ItemDragSelectLocation;
    UI_AddCancelButton(clent);
}

CLIENTCOMMAND(DropItem) {
    edict_t *unit;
    int32_t slot;

    if (!clent || !clent->client || argc < 2) {
        return;
    }
    unit = G_GetInventoryInteractionUnit(clent->client);
    slot = atoi(argv[1]);
    if (!G_UnitCanControl(clent->client, unit) || !G_InventoryCanDropItems(unit) ||
        slot < 0 || (uint32_t)slot >= G_InventoryCapacity(unit)) {
        return;
    }
    G_DropItem(unit, (uint32_t)slot);
}

static void G_PublishEndCinematicForHumans(edict_t *clent, bool debug_log) {
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
        edict_t *ent = G_GetPlayerEntityByNumber(i);
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
    if (G_CancelTargetMode(clent)) {
        return;
    }
    G_PublishEndCinematicForHumans(clent, true);
}

void UI_ShowQuest(edict_t *ent, quest_t const *quest);

CLIENTCOMMAND(Quests) {
    UI_ShowQuests(clent);
}

CLIENTCOMMAND(Log) {
    UI_ShowLog(clent);
}

CLIENTCOMMAND(HideGameResult) {
    uint32_t result = clent && clent->client ? clent->client->ps.stats[PLAYERSTATE_GAME_RESULT] : 3;
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

CLIENTCOMMAND(MenuRestart) {
    (void)clent; (void)argc; (void)argv;
    if (!G_IsSinglePlayer()) return;
    G_RequestRestartGame(false);
}

CLIENTCOMMAND(MenuQuitGame) {
    (void)clent; (void)argc; (void)argv;
    G_RequestQuitGame();
}

CLIENTCOMMAND(MenuConfirmExit) {
    (void)argc; (void)argv;
    UI_ShowGameMenuConfirmExit(clent);
}

CLIENTCOMMAND(MenuSaveGame) {
    (void)argc; (void)argv;
    UI_ShowGameMenuSave(clent);
}

CLIENTCOMMAND(MenuLoadGame) {
    (void)argc; (void)argv;
    UI_ShowGameMenuLoad(clent);
}

static bool MenuNormalizeSaveName(cstring_t input, string_t out, uint32_t out_size) {
    cstring_t begin, end;
    uint32_t len;

    if (!input || !out || out_size == 0) return false;
    out[0] = '\0';
    begin = input;
    while (*begin && isspace((unsigned char)*begin)) begin++;
    end = begin + strlen(begin);
    while (end > begin && isspace((unsigned char)end[-1])) end--;
    len = (uint32_t)(end - begin);
    if (!len || len >= out_size) return false;
    snprintf(out, out_size, "%.*s", (int)len, begin);

    len = (uint32_t)strlen(out);
    if (len >= 4 && !strcasecmp(out + len - 4, ".sav")) {
        out[len - 4] = '\0';
        len -= 4;
    }
    if (!len || !strcmp(out, ".") || !strcmp(out, "..") || out[len - 1] == '.')
        return false;
    for (uint32_t i = 0; out[i]; i++) {
        unsigned char ch = (unsigned char)out[i];
        if (ch < 0x20 || strchr("\\/:*?\"<>|", ch)) return false;
    }
    if (!strcasecmp(out, "Quick Save")) snprintf(out, out_size, "quick");
    return out[0] != '\0';
}

CLIENTCOMMAND(MenuSaveNamed) {
    PATHSTR path = { 0 };
    char name[CMDARG_LEN] = { 0 };

    (void)clent;
    if (!G_IsSinglePlayer() || argc < 2 ||
        !MenuNormalizeSaveName(argv[1], name, sizeof(name))) {
        fprintf(stderr, "WC3 menu: invalid save name\n");
        return;
    }
    gi.SavePath(name, path, sizeof(path));
    if (path[0] && !WriteGame(path))
        fprintf(stderr, "WC3 menu: failed to save %s\n", path);
}

CLIENTCOMMAND(MenuLoadNamed) {
    char name[CMDARG_LEN] = { 0 };

    (void)clent;
    if (!G_IsSinglePlayer() || argc < 2 ||
        !MenuNormalizeSaveName(argv[1], name, sizeof(name))) return;
    G_RequestLoadGameNamed(name);
}

CLIENTCOMMAND(MenuDeleteNamed) {
    char name[CMDARG_LEN] = { 0 };

    if (!G_IsSinglePlayer() || argc < 2 || !MenuNormalizeSaveName(argv[1], name, sizeof(name))) {
        fprintf(stderr, "WC3 menu: invalid save name for deletion\n");
        return;
    }
    if (!gi.DeleteSave(name)) {
        fprintf(stderr, "WC3 menu: failed to delete save %s\n", name);
        return;
    }
    /* Replacing the unique window refreshes its rows without releasing modal pause ownership. */
    UI_ShowGameMenuSave(clent);
}

CLIENTCOMMAND(MenuSaveQuick) {
    cstring_t args[] = { "menu_save_named", "Quick Save" };
    CMD_MenuSaveNamed(clent, 2, args);
}

CLIENTCOMMAND(MenuLoadQuick) {
    (void)clent; (void)argc; (void)argv;
    if (!G_IsSinglePlayer()) return;
    /* Loading owns a full server/map rebuild. Reuse the established deferred
     * front-end bridge rather than invoking ReadGame inside a game callback. */
    G_RequestLoadGameMenu();
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
    UI_AlliesToggle(clent, (uint32_t)atoi(argv[1]), (PLAYERALLIANCE)atoi(argv[2]));
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

static void G_CheatPrintf(edict_t *clent, cstring_t fmt, ...) {
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
    if (clent && clent->client && clent->client->connected) {
        int32_t opcode = svc_console_print;
        gi.Write(PF_BYTE, &opcode);
        gi.Write(PF_STRING, text);
        gi.unicast(clent);
    }
}

static quest_t *G_QuestByOrdinal(uint32_t ordinal) {
    FOR_EACH_QUEST(q) {
        if (ordinal == 0) return q;
        ordinal--;
    }
    return NULL;
}

static void G_CheatCompleteQuest(quest_t *quest) {
    if (!quest) return;
    FOR_EACH_QUESTITEM(quest, item) item->completed = true;
    quest->completed = true;
}

static void G_CheatListQuests(edict_t *clent) {
    uint32_t ordinal = 0;

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

static void G_CheatCompleteQuestCommand(edict_t *clent, uint32_t argc, cstring_t argv[]) {
    uint32_t index;
    uint32_t count = 0;

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
    index = (uint32_t)strtoul(argv[2], NULL, 10);
    quest_t *quest = G_QuestByOrdinal(index);
    if (!quest) {
        G_CheatPrintf(clent, "WC3: quest %u does not exist", (unsigned)index);
        return;
    }
    G_CheatCompleteQuest(quest);
    G_CheatPrintf(clent, "WC3: completed quest %u and its objectives", (unsigned)index);
}

CLIENTCOMMAND(Quest) {
    uint32_t index;

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
    index = (uint32_t)strtoul(argv[1], NULL, 10);
    quest_t *quest = G_QuestByOrdinal(index);
    if (quest) UI_ShowQuest(clent, quest);
}

static bool G_TriggerFunctionMatches(cstring_t filter, struct jass_function const *func) {
    cstring_t name = jass_functionname(func);
    return !filter || !*filter || (name && strstr(name, filter));
}

static bool G_TriggerMatches(cstring_t filter, trigger_t *trigger) {
    if (!filter || !*filter) return true;
    FOR_EACH_LIST(gTriggerCondition_t, condition, trigger->conditions) {
        if (G_TriggerFunctionMatches(filter, condition->expr)) return true;
    }
    FOR_EACH_LIST(gTriggerAction_t, action, trigger->actions) {
        if (G_TriggerFunctionMatches(filter, action->func)) return true;
    }
    return false;
}

static void G_CheatPrintTriggerFunctions(edict_t *clent, trigger_t *trigger) {
    FOR_EACH_LIST(gTriggerCondition_t, condition, trigger->conditions) {
        cstring_t name = jass_functionname(condition->expr);
        G_CheatPrintf(clent, "    condition: %s", name ? name : "(anonymous)");
    }
    FOR_EACH_LIST(gTriggerAction_t, action, trigger->actions) {
        cstring_t name = jass_functionname(action->func);
        G_CheatPrintf(clent, "    action:    %s", name ? name : "(anonymous)");
    }
}

static void G_CheatListTriggers(edict_t *clent, cstring_t filter) {
    uint32_t shown = 0;

    if (!level.vm) {
        G_CheatPrintf(clent, "WC3: no active JASS VM");
        return;
    }
    FOR_LOOP(i, level.num_triggers) {
        trigger_t *trigger = &level.triggers[i];
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

static bool G_CheatFireTrigger(edict_t *clent, uint32_t index, bool use_selected, cstring_t label) {
    edict_t *unit = NULL;

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
    uint32_t index;
    bool use_selected = false;

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
    index = (uint32_t)strtoul(argv[2], NULL, 10);
    G_CheatFireTrigger(clent, index, use_selected, "firing");
}

static bool G_StringContainsNoCase(cstring_t text, cstring_t needle) {
    size_t needle_len;

    if (!text || !needle || !*needle) return false;
    needle_len = strlen(needle);
    while (*text) {
        if (!strncasecmp(text, needle, needle_len)) return true;
        text++;
    }
    return false;
}

static bool G_FunctionNameContainsAny(cstring_t name, cstring_t const words[], uint32_t count) {
    if (!name) return false;
    FOR_LOOP(i, count) {
        if (G_StringContainsNoCase(name, words[i])) return true;
    }
    return false;
}

static bool G_CinematicFunctionName(cstring_t name) {
    static cstring_t const markers[] = {
        "cinematic", "cutscene", "intro", "outro", "ending", "interlude"
    };
    static cstring_t const helpers[] = {
        "skip", "time_stop", "timestop", "cheat"
    };

    if (!name || G_FunctionNameContainsAny(name, helpers, sizeof(helpers) / sizeof(helpers[0]))) {
        return false;
    }
    return G_FunctionNameContainsAny(name, markers, sizeof(markers) / sizeof(markers[0]));
}

static bool G_TriggerLooksCinematic(trigger_t *trigger) {
    /* Classify by action names. Generated condition/helper names frequently
     * inherit words such as Intro without actually starting a cutscene. */
    FOR_EACH_LIST(gTriggerAction_t, action, trigger->actions) {
        if (G_CinematicFunctionName(jass_functionname(action->func))) return true;
    }
    return false;
}

static void G_CheatListCinematics(edict_t *clent, cstring_t filter) {
    uint32_t shown = 0;

    if (!level.vm) {
        G_CheatPrintf(clent, "WC3: no active JASS VM");
        return;
    }
    FOR_LOOP(i, level.num_triggers) {
        trigger_t *trigger = &level.triggers[i];
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

static bool G_ObjectiveFunctionName(cstring_t name) {
    static cstring_t const rejects[] = {
        "cheat", "defeat", "skip", "cinematic", "cutscene", "intro",
        "outro", "ending", "interlude", "time_stop", "timestop"
    };
    bool questish;
    bool completion;

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

static bool G_TriggerLooksObjective(trigger_t *trigger) {
    FOR_EACH_LIST(gTriggerAction_t, action, trigger->actions) {
        if (G_ObjectiveFunctionName(jass_functionname(action->func))) return true;
    }
    return false;
}

static void G_CheatListObjectives(edict_t *clent, cstring_t filter) {
    uint32_t shown = 0;

    if (!level.vm) {
        G_CheatPrintf(clent, "WC3: no active JASS VM");
        return;
    }
    FOR_LOOP(i, level.num_triggers) {
        trigger_t *trigger = &level.triggers[i];
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
    uint32_t index;
    bool use_selected = false;

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
    index = (uint32_t)strtoul(argv[2], NULL, 10);
    G_CheatFireTrigger(clent, index, use_selected, "completing objective via");
}

/* Short alias for the common campaign progression action:
 *   objc <trigger-index> [selected]
 * is exactly `objective complete <trigger-index> [selected]`. */
CLIENTCOMMAND(Objc) {
    cstring_t objective_argv[4] = { "objective", "complete", NULL, NULL };
    uint32_t objective_argc = MIN(argc + 1, (uint32_t)4);

    if (argc >= 2) objective_argv[2] = argv[1];
    if (argc >= 3) objective_argv[3] = argv[2];
    CMD_Objective(clent, objective_argc, objective_argv);
}

CLIENTCOMMAND(Cinematic) {
    uint32_t index;
    bool use_selected = false;

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
    index = (uint32_t)strtoul(argv[2], NULL, 10);
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

static bool G_DebugIsNumber(cstring_t text) {
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

static edict_t *G_PortraitCameraUnit(edict_t *clent, uint32_t argc, cstring_t argv[]) {
    uint32_t number;
    edict_t *target;
    if (!clent || !clent->client || argc < 2 || !G_DebugIsNumber(argv[1])) return NULL;
    number = (uint32_t)atoi(argv[1]);
    if (number >= globals.num_edicts) return NULL;
    target = &globals.edicts[number];
    if (!target->inuse || G_GetMainSelectedUnit(clent->client) != target || !G_IsEntitySelected(clent->client, target)) return NULL;
    return target;
}

static void CMD_PortraitCameraDown(edict_t *clent, uint32_t argc, cstring_t argv[]) {
    edict_t *target = G_PortraitCameraUnit(clent, argc, argv);
    if (!target || clent->client->no_control || clent->client->camera.target_controller) return;
    G_ClientSetCameraPosition(clent, &target->s.origin2);
    clent->client->camera.target_controller = target;
    clent->client->camera.target_offset = (vector2_t){ 0, 0 };
}

static void CMD_QuickCamera(edict_t *clent, uint32_t argc, cstring_t argv[]) {
    (void)argc;
    (void)argv;
    if (!clent || !clent->client || !clent->client->camera.quick_position_set) return;
    G_ClientSetCameraPosition(clent, &clent->client->camera.quick_position);
}

static void CMD_PortraitCameraUp(edict_t *clent, uint32_t argc, cstring_t argv[]) {
    uint32_t number;
    if (!clent || !clent->client || argc < 2 || !G_DebugIsNumber(argv[1])) return;
    number = (uint32_t)atoi(argv[1]);
    if (number >= globals.num_edicts || clent->client->camera.target_controller != &globals.edicts[number]) return;
    G_ClearCameraTarget(clent->client, "CMD_PortraitCameraUp");
}

/* Camera diagnostics use one command family so the in-game console stays
 * compact: `camera move <x> <y>` and `camera selected`.  The client owns
 * `camera edge <0|1>` because edge scrolling is local input state. */
CLIENTCOMMAND(Camera) {
    gameClient_t *client = clent ? clent->client : NULL;

    if (!client || argc < 2) {
        fprintf(stderr, "usage: camera <move <x> <y>|selected>\n");
        return;
    }
    if (!strcasecmp(argv[1], "move")) {
        vector2_t point;
        if (argc != 4 || !G_DebugIsNumber(argv[2]) || !G_DebugIsNumber(argv[3])) {
            fprintf(stderr, "usage: camera move <x> <y>\n");
            return;
        }
        point = (vector2_t){ (float)atoi(argv[2]), (float)atoi(argv[3]) };
        G_ClientSetCameraPosition(clent, &point);
        return;
    }
    if (!strcasecmp(argv[1], "selected")) {
        edict_t *target;
        if (argc != 2 || client->no_control || !(target = G_GetMainSelectedUnit(client))) {
            return;
        }
        G_ClientSetCameraPosition(clent, &target->s.origin2);
        client->camera.target_controller = target;
        client->camera.target_offset = (vector2_t){ 0, 0 };
        return;
    }
    fprintf(stderr, "usage: camera <move <x> <y>|selected>\n");
}

/* Remove nearby non-building enemy units around the selected friendly unit so
 * deterministic movement tests are not changed by campaign combat/crowding. */
CLIENTCOMMAND(EnemiesClear) {
    gameClient_t *client = clent ? clent->client : NULL;
    edict_t *center;
    vector2_t origin;
    float radius = WC3_ENEMIES_CLEAR_RADIUS;
    float radius_sq;
    uint32_t removed = 0;

    if (!G_CheatsEnabled()) {
        G_CheatPrintf(clent, "WC3: cheats are disabled; set sv_cheats 1");
        return;
    }
    if (argc > 2 || (argc == 2 && (!G_DebugIsNumber(argv[1]) || argv[1][0] == '-'))) {
        G_CheatPrintf(clent, "WC3: usage: enemiesclear [radius]");
        return;
    }
    if (argc == 2) radius = (float)atof(argv[1]);
    if (radius <= 0.0f) {
        G_CheatPrintf(clent, "WC3: enemiesclear radius must be positive");
        return;
    }
    center = client ? G_GetMainSelectedUnit(client) : NULL;
    if (!center || !G_UnitCanControl(client, center)) {
        G_CheatPrintf(clent, "WC3: enemiesclear requires a selected friendly unit");
        return;
    }

    origin = center->s.origin2;
    radius_sq = radius * radius;
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = &globals.edicts[i];
        float dx, dy;

        if (!ent->inuse || ent == center || !(ent->svflags & SVF_MONSTER) || !ent->data.UnitData ||
            G_UnitIsBuilding(ent->class_id) ||
            G_SelectionRelation(client->ps.number, ent) != SELECT_RELATION_ENEMY)
            continue;
        dx = ent->s.origin2.x - origin.x;
        dy = ent->s.origin2.y - origin.y;
        if (dx * dx + dy * dy > radius_sq) continue;
        G_FreeEdict(ent);
        removed++;
    }
    G_CheatPrintf(clent, "WC3: enemiesclear removed %u enemy unit%s around selected unit %u within %.0f",
                  (unsigned)removed, removed == 1 ? "" : "s",
                  (unsigned)(center - globals.edicts), radius);
}

CLIENTCOMMAND(DebugSpawn) {
    gameClient_t *client = clent->client;
    uint32_t class_id;
    vector2_t location;
    edict_t *spawned;
    uint32_t first_ability = 2;

    if (argc < 2 || strlen(argv[1]) < 4) {
        fprintf(stderr, "usage: debugspawn <unitid> [x y] [ability ...]\n");
        return;
    }

    class_id = *((uint32_t const *)argv[1]);
    location = (vector2_t){ client->ps.vieworigin.x, client->ps.vieworigin.y };
    if (argc >= 4 && G_DebugIsNumber(argv[2]) && G_DebugIsNumber(argv[3])) {
        location.x = atoi(argv[2]);
        location.y = atoi(argv[3]);
        first_ability = 4;
    } else {
        edict_t *selected = G_GetMainSelectedUnit(client);
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

    for (uint32_t i = first_ability; i < argc; i++) {
        if (strlen(argv[i]) >= 4) {
            unit_learnability(spawned, *((uint32_t const *)argv[i]));
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
    cstring_t name;
    void (*func)(edict_t *ent, uint32_t argc, cstring_t argv[]);
} clientCommand_t;

static void CMD_MusicFinished(edict_t *ent, uint32_t argc, cstring_t argv[]) {
    uint32_t session_id;
    wc3MusicSource_t source;

    if (!ent || !ent->client || argc < 2) return;
    session_id = (uint32_t)strtoul(argv[1], NULL, 10);
    if (!G_MusicAcceptFinished(ent->client, session_id)) return;
    source = ent->client->music.current_source;
    if (source == WC3_MUSIC_SOURCE_MAP) G_MusicMapTransitionFinished(ent->client);
    else if (source == WC3_MUSIC_SOURCE_EXPLICIT) G_MusicExplicitFinished(ent->client);
    else if (source == WC3_MUSIC_SOURCE_THEMATIC) G_MusicThematicFinished(ent->client);
}

static void CMD_MusicSelected(edict_t *ent, uint32_t argc, cstring_t argv[]) {
    uint32_t session_id, played_mask;
    int32_t index, position_ms;

    if (!ent || !ent->client || argc < 5) return;
    session_id = (uint32_t)strtoul(argv[1], NULL, 10);
    index = (int32_t)strtol(argv[2], NULL, 10);
    position_ms = (int32_t)strtol(argv[3], NULL, 10);
    played_mask = (uint32_t)strtoul(argv[4], NULL, 10);
    G_MusicTrackSelected(ent->client, session_id, index, position_ms, played_mask);
}

static void CMD_MusicSnapshot(edict_t *ent, uint32_t argc, cstring_t argv[]) {
    uint32_t thematic_session_id, restore_session_id, played_mask;
    int32_t index, position_ms;

    if (!ent || !ent->client || argc < 6) return;
    thematic_session_id = (uint32_t)strtoul(argv[1], NULL, 10);
    restore_session_id = (uint32_t)strtoul(argv[2], NULL, 10);
    index = (int32_t)strtol(argv[3], NULL, 10);
    position_ms = (int32_t)strtol(argv[4], NULL, 10);
    played_mask = (uint32_t)strtoul(argv[5], NULL, 10);
    G_MusicThematicSnapshot(ent->client, thematic_session_id, restore_session_id,
                            index, position_ms, played_mask);
}

/* The client reports the presentation class its window settled on (docs/architecture/ui-canvas.md).  Nothing
 * is authored here: the next resource-bar refresh re-sends LAYER_CONSOLE with or without extension chrome. */
static void CMD_UICanvas(edict_t *ent, uint32_t argc, cstring_t argv[]) {
    char *end = NULL;
    long value = argc > 1 && argv[1][0] ? strtol(argv[1], &end, 10) : -1;
    if (value < 0 || value >= UI_CANVAS_CLASS_COUNT || (end && *end)) {
        fprintf(stderr, "ui_canvas: rejected class \"%s\"\n", argc > 1 ? argv[1] : "");
        return;
    }
    ent->client->canvas = (UICANVASCLASS)value;
}

clientCommand_t clientCommands[] = {
    { "give", CMD_Give },
    { "god", CMD_God },
    { "kill", CMD_Kill },
    { "hero", CMD_Hero },
    { "win", CMD_Win },
    { "lose", CMD_Lose },
    { "day", CMD_Day },
    { "night", CMD_Night },
    { "instant", CMD_Instant },
    { "warpten", CMD_InstantBuild },
    { "button", CMD_Button },
    { "autocast", CMD_Autocast },
    { "research", CMD_Research },
    { "upgrade", CMD_Upgrade },
    { "inventory", CMD_Inventory },
    { "itemdrag", CMD_ItemDrag },
    { "cargounload", CMD_CargoUnload },
    { "dropitem", CMD_DropItem },
    { "select", CMD_Select },
    { "focus", CMD_Focus },
    { "cyclesubgroup", CMD_CycleSubgroup },
    { "+portraitcamera", CMD_PortraitCameraDown },
    { "-portraitcamera", CMD_PortraitCameraUp },
    { "quickcamera", CMD_QuickCamera },
    { "herobutton", CMD_HeroButton },
    { "herokey", CMD_HeroKey },
    { "idleworker", CMD_IdleWorker },
    { "point", CMD_Point },
    { "orderqueuerelease", CMD_OrderQueueRelease },
    { "smart", CMD_Smart },
    { "smartpoint", CMD_SmartPoint },
    { "cancel", CMD_Cancel },
    { "canceltrain", CMD_CancelTrain },
    { "quests", CMD_Quests },
    { "quest", CMD_Quest },
    { "trigger", CMD_Trigger },
    { "objective", CMD_Objective },
    { "objc", CMD_Objc },
    { "cinematic", CMD_Cinematic },
    { "jass", CMD_Jass },
    { "log", CMD_Log },
    { "hidegameresult", CMD_HideGameResult },
    { "music_finished", CMD_MusicFinished },
    { "music_selected", CMD_MusicSelected },
    { "music_snapshot", CMD_MusicSnapshot },
    { "gameresult_restart", CMD_GameResultRestart },
    { "gameresult_load", CMD_GameResultLoad },
    { "gameresult_quit", CMD_GameResultQuit },
    { "debugspawn", CMD_DebugSpawn },
    { "enemiesclear", CMD_EnemiesClear },
    { "eclear", CMD_EnemiesClear },
    { "sound_event", CMD_SoundEvent },
    { "camera", CMD_Camera },
    { "menu", CMD_Menu },
    { "menu_endgame", CMD_MenuEndGame },
    { "menu_restart", CMD_MenuRestart },
    { "menu_quit_game", CMD_MenuQuitGame },
    { "menu_confirm_exit", CMD_MenuConfirmExit },
    { "menu_save_game", CMD_MenuSaveGame },
    { "menu_load_game", CMD_MenuLoadGame },
    { "menu_save_named", CMD_MenuSaveNamed },
    { "menu_load_named", CMD_MenuLoadNamed },
    { "menu_delete_named", CMD_MenuDeleteNamed },
    { "menu_save_quick", CMD_MenuSaveQuick },
    { "menu_load_quick", CMD_MenuLoadQuick },
    { "resume", CMD_Resume },
    { "pause", CMD_Pause },
    { "ui_canvas", CMD_UICanvas },
    { "allies", CMD_Allies },
    { "allies_toggle", CMD_AlliesToggle },
    { "allies_toggle_victory", CMD_AlliesToggleVictory },
    { "allies_accept", CMD_AlliesAccept },
    { "allies_cancel", CMD_AlliesCancel },
    { NULL }
};

void G_ClientCommand(edict_t *ent, uint32_t argc, cstring_t argv[]) {
    for (clientCommand_t const *cmd = clientCommands; cmd->name; cmd++) {
        if (!strcmp(cmd->name, argv[0])) {
            cmd->func(ent, argc, argv);
            return;
        }
    }
}

void G_ClientSetCameraPosition(edict_t *ent, vector2_t const *position) {
    vector2_t clamped;

    if (ent->client->no_control)
        return;
    clamped = G_ClampCameraPosition(ent->client, position);
    G_ClearCameraTarget(ent->client, "G_ClientSetCameraPosition");
    ent->client->camera.target_height = ent->client->ps.vieworigin.z;
    ent->client->camera.old_state = ent->client->camera.state;
    ent->client->camera.state.position = clamped;
    ent->client->camera.start_time = G_Time();
    ent->client->camera.end_time = ent->client->camera.start_time;
}
