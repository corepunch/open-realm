#include "g_local.h"
#include "games/warcraft-3/common/minimap.h"

#define WC3_DEFAULT_MINIMAP_INDICATOR "UI\\Minimap\\Minimap-Ping.mdl"
#define WC3_DEFAULT_ALERT_PING_DURATION 1.0f
#define WC3_ATTACK_ADVISOR_AUTOMATIC_DURATION (-1.0f)

/* Serialize transient minimap presentation for one connected client. */
static void G_SendMinimapPingSized(gameClient_t *client, vec2_t const *position, float duration,
                                   color32_t color, uint32_t flags, float marker_size) {
    edict_t *clent;
    cstring_t model;

    if (!client || !position || duration <= 0.0f || !client->connected || !gi.MinimapPing) return;
    clent = G_GetPlayerEntityByNumber(client->ps.number);
    if (!clent || !clent->client) return;

    model = Theme_PlayerString(client, "MinimapIndicator", WC3_DEFAULT_MINIMAP_INDICATOR);
    gi.configstring(CS_MINIMAP, model && model[0] ? model : WC3_DEFAULT_MINIMAP_INDICATOR);
    gi.MinimapPing(clent, position, duration, color.a ? color : COLOR32_WHITE, flags, marker_size);
}

void G_SendMinimapPing(gameClient_t *client, vec2_t const *position, float duration, color32_t color, uint32_t flags) {
    G_SendMinimapPingSized(client, position, duration, color, flags, MINIMAP_PING_DEFAULT_SIZE);
}

/* Derive owner alerts from the completed entity so no alert state enters save/load. */
void G_SendOwnerMinimapAlert(edict_t *ent) {
    gameClient_t *client;

    if (!ent || ent->s.player >= MAX_PLAYERS) return;
    client = G_GetPlayerClientByNumber(ent->s.player);
    if (!client || client->ps.number != ent->s.player) return;
    G_SendMinimapPing(client, &ent->s.origin2, WC3_DEFAULT_ALERT_PING_DURATION,
                      COLOR32_WHITE, MINIMAP_PING_REMEMBER);
}


/* Automatic attack-alert policy is WC3 game logic, not a generic minimap feature. */
static uint32_t wc3_attack_alert_until[MAX_PLAYERS];

void G_ResetAttackAlerts(void) {
    memset(wc3_attack_alert_until, 0, sizeof(wc3_attack_alert_until));
}

static bool G_AttackAlertRecipientInRange(gameClient_t const *recipient, vec2_t const *position) {
    float range = game.constants.attackNotifyRange;
    if (!recipient || !position || range <= 0.0f) return false;
    /* BZ_COMPAT_GUESS: retail exposes AttackNotifyRange but its exact camera metric is not
     * documented. Camera-target world distance is the least invasive interpretation and
     * is isolated here for straightforward replacement after retail capture. */
    return Vector2_distance(&recipient->camera.state.position, position) < range;
}

static bool G_AttackAlertCanNotify(gameClient_t const *recipient, vec2_t const *position) {
    uint32_t player, now;
    if (!recipient || !recipient->connected || !position) return false;
    player = recipient->ps.number;
    if (player >= MAX_PLAYERS || G_AttackAlertRecipientInRange(recipient, position)) return false;
    now = G_Time();
    return !wc3_attack_alert_until[player] || now >= wc3_attack_alert_until[player];
}

static void G_AttackAlertCommitCooldown(gameClient_t const *recipient) {
    float delay;
    uint32_t millis;
    if (!recipient || recipient->ps.number >= MAX_PLAYERS) return;
    delay = MAX(0.0f, game.constants.attackNotifyDelay);
    millis = (uint32_t)MIN((double)UINT32_MAX, (double)delay * 1000.0);
    wc3_attack_alert_until[recipient->ps.number] = G_Time() + millis;
}

/* Retail CommandStrings comments out its [AdvisorStrings] header, leaving the
 * advisor keys under [Errors]. Prefer an explicit advisor section for overrides,
 * then read the stock legacy placement from Errors. */
static cstring_t G_AttackAlertAdvisorString(bool allied, bool town) {
    cstring_t key = allied
        ? (town ? "Allytownattack" : "Allyunderattack")
        : (town ? "Townattack" : "Unitattack");
    cstring_t value = FindConfigValue("AdvisorStrings", key);
    if (!value || !value[0]) value = FindConfigValue("Errors", key);
    return value && value[0] ? G_LevelString(value) : NULL;
}

/* Replace the one retail %s player-name token without treating map data as a C
 * printf format string. Unknown percent sequences remain literal. */
static cstring_t G_AttackAlertFormatAdvisor(cstring_t source, cstring_t player_name) {
    static char formatted[4][MAX_GAMECACHE_STRING];
    static uint32_t cursor;
    char *out = formatted[cursor++ & 3];
    char *write = out;
    size_t remaining = sizeof(formatted[0]);

    if (!source || !source[0]) return NULL;
    while (*source && remaining > 1) {
        if (source[0] == '%' && source[1] == 's') {
            cstring_t name = player_name ? player_name : "";
            size_t count = MIN(strlen(name), remaining - 1);
            memcpy(write, name, count);
            write += count;
            remaining -= count;
            source += 2;
            continue;
        }
        *write++ = *source++;
        remaining--;
    }
    *write = '\0';
    return out;
}

static void G_AttackAlertShowAdvisor(gameClient_t *recipient, gameClient_t const *owner,
                                     bool allied, bool town) {
    edict_t *clent;
    cstring_t text;

    if (!recipient) return;
    text = G_AttackAlertAdvisorString(allied, town);
    if (!text || !text[0]) return;
    if (allied) text = G_AttackAlertFormatAdvisor(text, owner && owner->ps.name ? owner->ps.name : "");
    clent = G_GetPlayerEntityByNumber(recipient->ps.number);
    if (!clent || !clent->client) return;

    /* BZ_COMPAT_GUESS: the AdvisorStrings wording and alert semantics are retail
     * data, but the exact advisor-message lifetime/fade is not recovered in-tree.
     * Reuse the gameplay message overlay's automatic text-duration policy until a
     * direct retail capture supplies dedicated timing. Transient text deliberately
     * stays out of the Message Log. */
    UI_ShowTransientText(clent, NULL, text, WC3_ATTACK_ADVISOR_AUTOMATIC_DURATION);
}

static void G_AttackAlertPlaySound(gameClient_t *recipient, bool allied, bool town) {
    edict_t *clent;
    cstring_t skin_key, alias;
    if (!recipient) return;
    skin_key = allied
        ? (town ? "AllyTownUnderAttackSound" : "AllyUnderAttackSound")
        : (town ? "TownAttackSound" : "UnderAttackSound");
    alias = Theme_PlayerString(recipient, skin_key, NULL);
    clent = G_GetPlayerEntityByNumber(recipient->ps.number);
    if (clent && alias && alias[0]) G_PlayUISoundForPlayer(clent, alias);
}

static void G_AttackAlertNotify(gameClient_t *recipient, edict_t *victim, bool allied, bool town) {
    /* BZ_COMPAT_GUESS: retail exposes a dedicated attacked-signal color, but the raw
     * MiscData key and exact signal lifetime have not yet been recovered in-tree.
     * Use the observed red attack signal and the existing one-second alert lifetime;
     * keep both confined here so data-backed values can replace them without changing
     * contacts, combat, or the generic minimap wire contract. */
    color32_t const attacked_signal = MAKE(color32_t, 255, 0, 0, 255);
    if (!recipient || !victim || !G_AttackAlertCanNotify(recipient, &victim->s.origin2)) return;
    entityState_t marker_state = victim->s;
    /* Match G_CustomizeEntity's recipient-specific visibility adjustment before
     * classifying the marker, so a true-sight Hero keeps its larger footprint. */
    if ((marker_state.renderfx & RF_HIDDEN) && S_UnitUsesInvisibilityRenderFlag(victim) &&
        !S_UnitIsInvisibleToPlayer(victim, recipient->ps.number))
        marker_state.renderfx &= ~RF_HIDDEN;
    wc3MinimapContact_t const contact = G_WC3_MinimapMarkerForEntity(victim, &marker_state);
    float const marker_size = wc3_minimap_contact_size(contact != WC3_MINIMAP_CONTACT_NONE ? contact : WC3_MINIMAP_CONTACT_UNIT);
    G_AttackAlertPlaySound(recipient, allied, town);
    G_AttackAlertShowAdvisor(recipient, G_GetPlayerClientByNumber(victim->s.player), allied, town);
    G_SendMinimapPingSized(recipient, &victim->s.origin2, WC3_DEFAULT_ALERT_PING_DURATION,
                           attacked_signal, MINIMAP_PING_REMEMBER | MINIMAP_PING_FORCE_COLOR, marker_size);
    G_AttackAlertCommitCooldown(recipient);
}

/* BZ_COMPAT_GUESS: use the resolved normal-weapon hit as the alarm producer. Retail
 * clearly distinguishes attack alarms from arbitrary spell/script damage, but whether a
 * miss/evade should alarm at swing/launch time still needs direct capture. Keeping this
 * entry point out of T_Damage prevents DoT and scripted health damage from alerting. */
void G_WC3_AttackAlert(edict_t *victim, edict_t *attacker) {
    gameClient_t *owner;
    bool town;

    if (!victim || !attacker || victim->s.player >= MAX_PLAYERS || attacker->s.player >= MAX_PLAYERS ||
        victim->s.player == attacker->s.player || !S_SpellIsEnemy(attacker, victim)) return;
    owner = G_GetPlayerClientByNumber(victim->s.player);
    if (!owner || owner->ps.number != victim->s.player) return;

    /* BZ_COMPAT_GUESS: the stock skin distinguishes forces/town sounds, but the exact
     * retail town classifier is not documented. Treat authored buildings as town alerts
     * for now and keep the decision in one helper-sized expression for later correction. */
    town = G_UnitIsBuilding(victim->class_id);
    G_AttackAlertNotify(owner, victim, false, town);

    FOR_LOOP(i, game.max_clients) {
        gameClient_t *recipient = game.clients + i;
        if (recipient == owner || recipient->ps.number >= MAX_PLAYERS) continue;
        /* ALLIANCE_HELP_REQUEST is directional in WC3: the attacked owner chooses
         * whether this ally receives automatic help-request notifications. */
        if (!G_GetPlayerAlliance(&owner->ps, &recipient->ps, ALLIANCE_PASSIVE) ||
            !G_GetPlayerAlliance(&owner->ps, &recipient->ps, ALLIANCE_HELP_REQUEST)) continue;
        G_AttackAlertNotify(recipient, victim, true, town);
    }
}

/* Cursor color follows the skin's team-color image, not a hardcoded RGB palette. */
uint32_t G_SignalColorImage(gameClient_t *client) {
    cstring_t prefix = Theme_PlayerString(client, "TeamColor", NULL);
    cstring_t count_text = Theme_PlayerString(client, "TeamColors", NULL);
    uint32_t count = count_text ? strtoul(count_text, NULL, 10) : 0;
    uint32_t color = client->ps.color;
    PATHSTR path;
    if (!prefix || !*prefix || !count) {
        fprintf(stderr, "WC3 signal: missing or invalid TeamColor/TeamColors skin fields\n");
        return 0;
    }
    color %= count;
    if (client->ps.stats[WC3_PLAYERSTAT_MINIMAP_ALLY_COLOR] == WC3_MINIMAP_ALLY_COLOR_WORLD) {
        cstring_t index = Stb_IniCacheFind(&game.config.misc, "TeamColorFilter", "ColorIndexPlayer");
        if (!index || !*index) {
            fprintf(stderr, "WC3 signal: missing TeamColorFilter.ColorIndexPlayer\n");
            return 0;
        }
        color = strtoul(index, NULL, 10);
        if (color >= count) {
            fprintf(stderr, "WC3 signal: ColorIndexPlayer %u exceeds TeamColors %u\n", color, count);
            return 0;
        }
    }
    snprintf(path, sizeof(path), "%s%02u.blp", prefix, color);
    return gi.ImageIndex(path);
}

/* Signal is an overlay, not a replacement target callback. Right click/Esc
 * pops it; a left click emits an ally ping and restores the underlying command. */
bool G_SignalCommand(edict_t *ent, uint32_t argc, cstring_t argv[]) {
    gameClient_t *client = ent ? ent->client : NULL;
    vec2_t position;
    if (!client || !argc) return false;
    if (!strcmp(argv[0], "signal")) {
        client->cursor_signal = true;
        return true;
    }
    if (!client->cursor_signal) return false;
    if (!strcmp(argv[0], "cancel") || !strcmp(argv[0], "smart") || !strcmp(argv[0], "smartpoint")) {
        client->cursor_signal = false;
        return true;
    }
    if (!strcmp(argv[0], "point") && argc >= 3) {
        position = (vec2_t){atof(argv[1]), atof(argv[2])};
        if (!isfinite(position.x) || !isfinite(position.y)) return true;
    } else if (!strcmp(argv[0], "select") && argc >= 2) {
        char *end;
        unsigned long number = strtoul(argv[1], &end, 10);
        if (*end || !number || number >= globals.num_edicts || !g_edicts[number].inuse) return true;
        position = g_edicts[number].s.origin2;
    } else return false;
    client->cursor_signal = false;
    FOR_LOOP(i, game.max_clients) {
        gameClient_t *recipient = game.clients + i;
        if (recipient == client || G_GetPlayerAlliance(&client->ps, &recipient->ps, ALLIANCE_PASSIVE))
            G_SendMinimapPing(recipient, &position, WC3_DEFAULT_ALERT_PING_DURATION,
                             COLOR32_WHITE, MINIMAP_PING_REMEMBER);
    }
    return true;
}
