#include "s_skills.h"
#ifdef BZ_TESTS
#include "test.h"
#endif

#include <ctype.h>
#include <float.h>
#include <math.h>

#define DEFAULT_SPELL_AREA_CURSOR "ReplaceableTextures\\Selection\\SpellAreaOfEffect.blp"
typedef struct {
    edict_t *caster;
    uint32_t code, level;
    ability_t const *spell;
    edict_t *target;
    edict_t *source_item;
    uint32_t source_item_spawn_time;
} spellUnitTargetParams_t;

typedef struct {
    edict_t *clent, *caster;
    uint32_t code, level;
    vec2_t const *point;
    float range;
    edict_t *source_item;
} spellPointValidateParams_t;

/* ---- Unified Spell Pipeline ----

 * All hero/unit spells route through a single cmd entry point (spell_cmd) that
 * reads the ability code, validates mana/cooldown, configures targeting, and
 * dispatches an A_EXECUTE message once a valid target is
 * acquired.  channeled spells set G_GetChannel(ent) state; spell_run_frame()
 * enforces movement-cancel for them.
 *
 * Design mirrors:
 *   - WarSmash: CAbilitySpellBase with target-type dispatch
 *   - WoW: data-driven spell table + cast state machine (Wow_RunSpellCast)
 *   - Quake2: flat flags and callbacks around a shared processor */

/* Build the borrowed typed payload used by the synchronous spell messages. */
static intptr_t spell_message(edict_t *ent, abilityMsg_t msg, abilityitem_t const *item, spellTarget_t const *target) {
    abilityCall_t call = MAKE(abilityCall_t, .item = item, .target = target);
    return S_AbilityMessage(ent, msg, &call);
}

BZ_ABILITY_PROC(CAbilityNoop) {
    (void)ent; (void)msg; (void)call;
    return false;
}

BZ_ABILITY_PROC(CAbilityPassive) {
    return CAbilityNoop(ent, msg, call);
}

/* CAbilitySimpleSpell owns the shared command path and accepts validation
 * unless a concrete TFT procedure supplies stricter target rules. */
BZ_ABILITY_PROC(CAbilitySimpleSpell) {
    channel_t const *cast;
    (void)ent;
    if (!call || !call->item || !call->item->ability) return false;
    switch (msg) {
    case A_COMMAND:
        if (!call->entity) return false;
        spell_cmd(call->entity);
        return true;
    case A_TARGET_MODE_EXIT:
        S_SpellCursorSplat(call->entity, 0.0f);
        return true;
    case A_VALIDATE: return true;
    case A_MOVE_LEAVE:
        /* Shared fallback for many procedures: only movement reads the channel. */
        cast = G_GetChannel(ent);
        if (ent && cast && cast->code == call->item->code) S_SpellCancelChannel(ent);
        return true;
    default:
        return false;
    }
}

/* Modal spells share autocast selection while concrete children own acquisition and effects. */
BZ_ABILITY_PROC(CAbilityModalSpell) {
    uint32_t code = call && call->item ? call->item->code : 0;
    switch (msg) {
    case A_AUTOCAST_ON: return ent && ent->unit_state->autocast == code;
    case A_AUTOCAST_SET: return true;
    default: return CAbilitySimpleSpell(ent, msg, call);
    }
}

void S_SpellCodeString(uint32_t code, string_t out) {
    memcpy(out, &code, 4);
    out[4] = '\0';
}

uint32_t S_SpellCurrentCode(edict_t *clent, uint32_t fallback) {
    uint32_t code = clent && clent->client ? clent->client->menu.ability_code : 0;
    return code ? code : fallback;
}

ability_t const *S_SpellAbilityForCode(uint32_t code) {
    ability_t const *ability;

    if (!code) return NULL;

    /* A rawcode stored in a uint32_t is four bytes, not a C string. The old
     * spell path cast &code to cstring_t and handed it to strcmp(), so lookup
     * depended on the unrelated byte immediately after the uint32_t being zero.
     * Convert through GetClassName(), which supplies the required terminator,
     * and use the normal alias-aware command resolver so custom abilities
     * inherit their registered base handler as well. */
    ability = FindAbilityForCommand(GetClassName(code));
    return ability && (ability->flags & AB_SPELL) && ability->proc ? ability : NULL;
}

uint32_t S_SpellLevel(edict_t *caster, uint32_t code) {
    if (!caster) {
        return 1;
    }
    FOR_LOOP(i, caster->unit_state ? MAX_HERO_ABILITIES : 0) {
        heroability_t const *ha = caster->unit_state->ranks + i;
        if (ha->level && ha->code == code) {
            return ha->level;
        }
    }
    return 1;
}

float S_SpellNumber(uint32_t code, abilityNumber_t field, uint32_t level) {
    abilityLevel_t const *row = G_AbilityLevel(code, level);
    switch (field) {
    case ABILITY_NUMBER_CAST: return row->cast;
    case ABILITY_NUMBER_DURATION: return row->dur;
    case ABILITY_NUMBER_HERO_DURATION: return row->heroDur;
    case ABILITY_NUMBER_COOLDOWN: return row->cool;
    case ABILITY_NUMBER_COST: return row->cost;
    case ABILITY_NUMBER_AREA: return row->area;
    case ABILITY_NUMBER_RANGE: return row->range;
    }
    return 0.0f;
}

cstring_t S_SpellString(uint32_t code, cstring_t field, uint32_t level) {
    char code_string[5];
    cstring_t value;

    if (!field || !*field) {
        return NULL;
    }

    S_SpellCodeString(code, code_string);
    value = FindConfigValue(code_string, field);
    if (!value || !strcmp(value, "-") || !strcmp(value, "_")) {
        return NULL;
    }
    if (!level) return value;
    PARSE_LIST(value, perlevel, parse_segment) {
        if (--level == 0) return perlevel;
    }
    return value;
}

/* Data slots use the shared ROC/TFT schema resolver; index is 1-based. */
float S_SpellData(uint32_t code, uint32_t level, uint32_t index) {
    char classname[5] = {0};
    memcpy(classname, &code, 4);
    return AB_Data(classname, level, index);
}

uint32_t S_SpellDataId(uint32_t code, uint32_t level, uint32_t index) {
    char classname[5] = {0};
    memcpy(classname, &code, 4);
    return AB_DataId(classname, level, index);
}

uint32_t S_SpellUnitId(uint32_t code, uint32_t level) {
    return G_AbilityLevel(code, level)->unitID;
}

float S_SpellRange(uint32_t code, uint32_t level) {
    return S_SpellNumber(code, ABILITY_NUMBER_RANGE, level);
}

float S_SpellDuration(uint32_t code, uint32_t level, bool hero) {
    return S_SpellNumber(code, hero ? ABILITY_NUMBER_HERO_DURATION : ABILITY_NUMBER_DURATION, level);
}

static void S_SpellInvalidateCooldownUI(edict_t *caster) {
    edict_t *player;
    gameClient_t *client;
    if (!caster) return;
    client = G_GetPlayerClientByNumber(caster->s.player);
    if (!client || client->ps.number != caster->s.player) return;
    G_InvalidateCommands(client);
    /* Inventory command buttons carry their own radial cooldown payload. The
     * command-card dirty bit does not resend LAYER_INVENTORY, so refresh that
     * layer whenever an ability cooldown starts, changes, or ends. */
    player = G_GetPlayerEntityByNumber(caster->s.player);
    if (player && player->client == client) G_RefreshInventoryLayer(player);
}

static uint32_t S_SpellCooldownCode(uint32_t code) {
    return code ? G_AbilityCode(code) : 0;
}

static abilityCooldown_t *S_SpellFindCooldown(edict_t *caster, uint32_t code) {
    uint32_t const cooldown_code = S_SpellCooldownCode(code);

    if (!caster || !cooldown_code) return NULL;
    FOR_LOOP(i, caster->unit_state ? MAX_UNIT_COOLDOWNS : 0) {
        abilityCooldown_t *cooldown = caster->unit_state->cooldowns + i;
        if (cooldown->code == cooldown_code) return cooldown;
    }
    return NULL;
}

static abilityCooldown_t *S_SpellAllocCooldown(edict_t *caster, uint32_t code) {
    uint32_t const cooldown_code = S_SpellCooldownCode(code);
    uint32_t const now = G_Time();
    abilityCooldown_t *available = NULL;

    if (!caster || !cooldown_code) return NULL;
    G_InitUnitState(caster);
    FOR_LOOP(i, caster->unit_state ? MAX_UNIT_COOLDOWNS : 0) {
        abilityCooldown_t *cooldown = caster->unit_state->cooldowns + i;
        if (cooldown->code == cooldown_code) return cooldown;
        if (!available && (!cooldown->code || (int32_t)(cooldown->end_time - now) <= 0)) available = cooldown;
    }
    if (available) {
        memset(available, 0, sizeof(*available));
        available->code = cooldown_code;
    }
    return available;
}

bool S_SpellCooldownReady(edict_t *caster, uint32_t code) {
    abilityCooldown_t const *cooldown = S_SpellFindCooldown(caster, code);
    return !cooldown || (int32_t)(cooldown->end_time - G_Time()) <= 0;
}

float S_SpellCooldownRemaining(edict_t *caster, uint32_t code) {
    abilityCooldown_t const *cooldown = S_SpellFindCooldown(caster, code);
    uint32_t const now = G_Time();
    if (!cooldown || (int32_t)(cooldown->end_time - now) <= 0) return 0.0f;
    return (float)(uint32_t)(cooldown->end_time - now) / 1000.0f;
}

float S_SpellCooldownLength(edict_t *caster, uint32_t code) {
    abilityCooldown_t const *cooldown = S_SpellFindCooldown(caster, code);
    if (!cooldown || cooldown->end_time == cooldown->start_time) return 0.0f;
    return (float)(uint32_t)(cooldown->end_time - cooldown->start_time) / 1000.0f;
}

bool S_SpellCooldownWindow(edict_t *caster, uint32_t code, abilityCooldownWindow_t *window) {
    abilityCooldown_t const *cooldown = S_SpellFindCooldown(caster, code);
    if (!cooldown || !window || (int32_t)(cooldown->end_time - G_Time()) <= 0) return false;
    window->start_time = cooldown->start_time;
    window->end_time = cooldown->end_time;
    return true;
}

/* Fraction of an ability's authored cooldown still remaining. The total comes
 * from the cooldown record captured at cast time, so learning another level
 * while a cooldown is active cannot make the command-card sweep jump. */
float S_SpellCooldownFraction(edict_t *caster, uint32_t code, uint32_t level) {
    float const remaining = S_SpellCooldownRemaining(caster, code);
    float const total = S_SpellCooldownLength(caster, code);
    (void)level;
    if (remaining <= 0.0f || total <= 0.0f) return 0.0f;
    return MIN(1.0f, remaining / total);
}

void S_SpellStartCooldownDuration(edict_t *caster, uint32_t code, float duration) {
    abilityCooldown_t *cooldown;
    uint32_t duration_ms;

    if (!caster || !code) return;
    if (duration <= 0.0f) {
        S_SpellEndCooldown(caster, code);
        return;
    }
    cooldown = S_SpellAllocCooldown(caster, code);
    if (!cooldown) return;
    duration_ms = (uint32_t)ceilf(duration * 1000.0f);
    cooldown->code = S_SpellCooldownCode(code);
    cooldown->start_time = G_Time();
    cooldown->end_time = cooldown->start_time + MAX(duration_ms, 1u);
    S_SpellInvalidateCooldownUI(caster);
}

void S_SpellStartCooldown(edict_t *caster, uint32_t code, uint32_t level) {
    S_SpellStartCooldownDuration(caster, code,
        S_SpellNumber(code, ABILITY_NUMBER_COOLDOWN, level));
}

void S_SpellEndCooldown(edict_t *caster, uint32_t code) {
    abilityCooldown_t *cooldown = S_SpellFindCooldown(caster, code);
    if (cooldown) {
        memset(cooldown, 0, sizeof(*cooldown));
        S_SpellInvalidateCooldownUI(caster);
    }
}

void S_SpellResetCooldowns(edict_t *caster) {
    if (!caster) return;
    if (!caster->unit_state) return;
    memset(caster->unit_state->cooldowns, 0, sizeof(caster->unit_state->cooldowns));
    S_SpellInvalidateCooldownUI(caster);
}

bool S_SpellSpendMana(edict_t *caster, uint32_t code, uint32_t level) {
    float cost;

    if (!caster) {
        return false;
    }
    cost = S_SpellNumber(code, ABILITY_NUMBER_COST, level);
    if (cost <= 0) {
        return true;
    }
    if (caster->mana.value < cost) {
        return false;
    }
    caster->mana.value -= cost;
    return true;
}

bool S_SpellCanPay(edict_t *caster, uint32_t code, uint32_t level) {
    float cost;

    if (!caster) {
        return false;
    }
    cost = S_SpellNumber(code, ABILITY_NUMBER_COST, level);
    return cost <= 0 || caster->mana.value >= cost;
}

bool S_SpellTargetInRange(edict_t *caster, edict_t *target, float range) {
    if (!caster || !target) {
        return false;
    }
    return range <= 0 || Vector2_distance(&caster->s.origin2, &target->s.origin2) <= range;
}

static bool spell_is_tree_interaction(ability_t const *spell) {
    return spell && (spell->proc == CAbilityGrabTree || spell->proc == CAbilityEatTree);
}

static float spell_tree_target_range(uint32_t code, uint32_t level, edict_t const *caster) {
    return MAX(S_SpellRange(code, level), 32.0f) +
        MAX(0.0f, caster ? caster->collision : 0.0f);
}

/* Trees block movement through their authored pathing texture, despite often
 * having zero collision radius. Measure interaction range to that footprint
 * so a Giant can cast from the nearest legal cell without entering the tree. */
static float spell_tree_distance_from_point(edict_t const *tree, vec2_t const *point,
                                           uint8_t blocked_flags) {
    float distance;
    if (!tree || !point) return FLT_MAX;
    if (tree->pathtex) {
        distance = CM_DistanceToPathingFootprintFlags(tree, point, blocked_flags);
        if (distance < FLT_MAX) return distance;
    }
    return MAX(0.0f, Vector2_distance(point, &tree->s.origin2) - MAX(0.0f, tree->collision));
}

static float spell_tree_distance(edict_t const *caster, edict_t const *tree) {
    return caster ? spell_tree_distance_from_point(tree, &caster->s.origin2,
                                                    M_UnitStaticPathingFlags(caster)) : FLT_MAX;
}

static float spell_approach_range(uint32_t code, uint32_t level, ability_t const *spell) {
    abilityCall_t call;
    if (!spell) return S_SpellRange(code, level);
    abilityitem_t item = MAKE(abilityitem_t, .code = code, .ability = spell);
    float const authored_range = S_SpellRange(code, level);
    call = MAKE(abilityCall_t, .item = &item, .spell_approach_range = authored_range);
    spell->proc(NULL, A_SPELL_APPROACH_RANGE, &call);
    return MAX(0.0f, call.spell_approach_range);
}

bool S_SpellIsAliveTarget(edict_t *target) {
    return G_UnitIsWorldActive(target) && (target->svflags & SVF_MONSTER) && !M_IsDead(target);
}

bool S_SpellIsEnemy(edict_t *caster, edict_t *target) {
    uint32_t owner;

    if (!caster || !target || caster->s.player >= MAX_PLAYERS || target->s.player >= MAX_PLAYERS) {
        return false;
    }
    owner = target->s.player;
    if (caster->s.player == owner) {
        return false;
    }
    if (owner < PLAYER_NEUTRAL_AGGRESSIVE && level.mapinfo &&
        level.mapinfo->players[owner].playerType == kPlayerTypeNone) {
        return false;
    }
    return !G_PlayerTreatsPlayerAsAlly(caster->s.player, owner);
}

bool S_SpellIsFriend(edict_t *caster, edict_t *target) {
    uint32_t owner;

    if (!caster || !target || caster->s.player >= MAX_PLAYERS || target->s.player >= MAX_PLAYERS) {
        return false;
    }
    owner = target->s.player;
    if (caster->s.player == owner) {
        return true;
    }
    if (owner < PLAYER_NEUTRAL_AGGRESSIVE && level.mapinfo &&
        level.mapinfo->players[owner].playerType == kPlayerTypeNone) {
        return false;
    }
    return G_PlayerTreatsPlayerAsAlly(caster->s.player, owner);
}

bool S_SpellTargetHasToken(cstring_t targets, cstring_t full, cstring_t short_name) {
    char token[32];
    cstring_t cursor = targets;

    while (cursor && *cursor) {
        size_t len = 0;
        while (*cursor == ',' || isspace((unsigned char)*cursor)) cursor++;
        while (*cursor && *cursor != ',' && len + 1 < sizeof(token)) token[len++] = *cursor++;
        while (len && isspace((unsigned char)token[len - 1])) len--;
        token[len] = '\0';
        if (!strcasecmp(token, full) || (short_name && !strcasecmp(token, short_name))) return true;
        while (*cursor && *cursor != ',') cursor++;
    }
    return false;
}

bool S_SpellAllowsTarget(uint32_t code, edict_t *caster, edict_t *target) {
    /* Sentinel is a tree-target spell. Ordinary unit-target masks are not
     * authoritative for destructables (which have no unit relationship). */
    if (G_AbilityCode(code) == MAKEFOURCC('A','e','s','n'))
        return target && target->targtype == TARG_TREE &&
               G_DestructableIsAttackable(target);
    /* Unit-target abilities authored with the dead token must pass the same
     * corpse target rules as their ability procedure during normal orders. */
    if (target && M_IsDead(target)) {
        abilityLevel_t const *row = G_AbilityLevel(code, S_SpellLevel(caster, code));
        return row && S_SpellTargetHasToken(row->targs, "dead", NULL) &&
               S_SpellAllowsCorpseTarget(code, caster, target) &&
               !S_UnitIsInvisibleToPlayer(target, caster->s.player);
    }
    if (S_ImpaleIsAirborne(target)) return false;
    if (!S_SpellAllowsAreaTarget(code, caster, target)) return false;
    return !(caster && caster->s.player < MAX_PLAYERS &&
             S_UnitIsInvisibleToPlayer(target, caster->s.player));
}

/* Spell Shield intercepts hostile unit-targeted spells at resolution. Launched
 * bolts defer the check until impact so shield state can change in flight. */
bool S_TryBlockSpellShield(edict_t *caster, uint32_t code, edict_t *target) {
    ability_t const *ability = S_SpellAbilityForCode(code);
    abilityLevel_t const *row;
    heroabilitystatus_t *status;

    if (!caster || !target || !ability || ability->proc == CAbilityDevour ||
        (ability->target_type != SPELL_TARGET_UNIT && ability->target_type != SPELL_TARGET_UNIT_OR_POINT) ||
        !S_SpellIsEnemy(caster, target) || !S_SpellIsAliveTarget(target)) return false;
    /* Physical nets and Finger of Death bypass Spell Shield. */
    if (ability->proc == CAbilityEnsnare || ability->proc == CAbilityWeb ||
        ability->proc == CAbilityFingerOfDeath) return false;
    row = G_AbilityLevel(code, S_SpellLevel(caster, code));
    /* Mixed masks (for example Death Coil and Siphon Mana) have hostile and
     * friendly branches. The target's actual relationship selects the branch. */
    if (!row || !row->targs || !S_SpellTargetHasToken(row->targs, "enemy", NULL)) return false;
    status = unit_findstatus(target, MAKEFOURCC('B', 'N', 's', 's'));
    if (!status) return false;
    unit_removestatus(target, status, STATUS_REMOVE_SCRIPT);
    unit_refreshstatusflags(target);
    G_InvalidateUnitInfoPanel(target);
    return true;
}

bool S_SpellShieldImpactDeferred(uint32_t code) {
    ability_t const *ability = S_SpellAbilityForCode(code);
    return ability && (ability->proc == CAbilityThunderBolt || ability->proc == CAbilityFireBolt ||
                       ability->proc == CAbilityStormBoltCampaign || ability->proc == CAbilityDeathCoil ||
                       ability->proc == CAbilityAcidBomb || ability->proc == CAbilityTransmute);
}

/* The authored targs mask without the unit-target visibility rule: area effects reach
 * units the caster cannot see. */
bool S_SpellAllowsAreaTarget(uint32_t code, edict_t *caster, edict_t *target) {
    cstring_t targets;
    uint32_t ability_level;
    bool structure;

    if (!G_UnitIsWorldActive(target) || M_IsDead(target) || S_UnitIsCycloned(target)) {
        return false;
    }
    if (S_ImpaleIsAirborne(target) && !S_ImpaleAirborneVulnerable(target)) return false;
    /* Devour is a physical swallowing ability, not a magic projectile. */
    { ability_t const *ability = S_SpellAbilityForCode(code);
      /* Area target masks are also shared by non-spell mechanics such as
       * Cleaving Attack; resolve their registered parent class too. */
      if (!ability) {
          abilityitem_t item = S_AbilityItem(code);
          ability = item.ability;
      }
      /* Taunt changes the target's attack order rather than applying a
       * hostile magical effect. Spell immunity alone does not make a unit
       * ineligible for its authored enemy target mask. */
      if ((!ability || (ability->proc != CAbilityDevour && ability->proc != CAbilityTaunt &&
                        ability->proc != CAbilityCleavingAttack)) &&
          S_UnitSpellImmune(target)) return false; }
    ability_level = S_SpellLevel(caster, code);
    targets = G_AbilityLevel(code, ability_level)->targs;
    if (!targets) {
        return true;
    }
    structure = G_UnitIsStructure(target);
    {
        bool const allows_hero = S_SpellTargetHasToken(targets, "hero", NULL);
        bool const allows_nonhero = S_SpellTargetHasToken(targets, "nonhero", "nonh");
        if ((allows_hero || allows_nonhero) &&
            (S_UnitIsResistant(target) ? !allows_hero : !allows_nonhero)) return false;
    }
    if (strstr(targets, "notself") && target == caster) return false;
    if ((strstr(targets, "air") || strstr(targets, "ground") || strstr(targets, "structure")) &&
        !(strstr(targets, "air") && G_UnitTargetType(target) == TARG_AIR) &&
        !(strstr(targets, "ground") && G_UnitTargetType(target) == TARG_GROUND) &&
        !(strstr(targets, "structure") && structure) &&
        !(S_SpellTargetHasToken(targets, "ward", NULL) && target->targtype == TARG_WARD)) {
        return false;
    }
    /* organic/mechanical are targtype tokens, not air/ground; TFT Cyclone authors organic. */
    if (strstr(targets, "organic") && target->targtype == TARG_MECHANICAL) return false;
    if (strstr(targets, "mechanical") && target->targtype != TARG_MECHANICAL) return false;
    if (strstr(targets, "friend") && S_SpellIsFriend(caster, target)) {
        return true;
    }
    if (strstr(targets, "player") && caster && target->s.player == caster->s.player) {
        return true;
    }
    if (strstr(targets, "ally") && caster && target->s.player != caster->s.player &&
        caster->s.player < MAX_PLAYERS && target->s.player < MAX_PLAYERS &&
        G_PlayerTreatsPlayerAsAlly(caster->s.player, target->s.player)) {
        return true;
    }
    if (strstr(targets, "enemy") && S_SpellIsEnemy(caster, target)) {
        return true;
    }
    if (strstr(targets, "neutral") && target->s.player < MAX_PLAYERS && level.mapinfo &&
        level.mapinfo->players[target->s.player].playerType == kPlayerTypeNeutral) {
        return true;
    }
    return !strstr(targets, "friend") && !strstr(targets, "player") && !strstr(targets, "ally") &&
        !strstr(targets, "enemy") && !strstr(targets, "neutral");
}

static bool spell_allows_corpse_target(uint32_t code, edict_t *caster, edict_t *target, bool stored) {
    cstring_t targets;
    uint32_t ability_level;
    bool structure;

    if (!caster || (stored ? !G_UnitIsRaisableStoredCorpse(target) :
                    !G_UnitIsRaisableCorpse(target))) return false;
    ability_level = S_SpellLevel(caster, code);
    targets = G_AbilityLevel(code, ability_level)->targs;
    if (!targets) return true;

    structure = G_UnitIsStructure(target);
    if ((strstr(targets, "air") || strstr(targets, "ground") || strstr(targets, "structure")) &&
        !(strstr(targets, "air") && G_UnitTargetType(target) == TARG_AIR) &&
        !(strstr(targets, "ground") && G_UnitTargetType(target) == TARG_GROUND) &&
        !(strstr(targets, "structure") && structure)) return false;
    if (strstr(targets, "organic") && target->targtype == TARG_MECHANICAL) return false;
    if (strstr(targets, "mechanical") && target->targtype != TARG_MECHANICAL) return false;

    if (strstr(targets, "player") && target->s.player == caster->s.player) return true;
    if (strstr(targets, "friend") && S_SpellIsFriend(caster, target)) return true;
    if (strstr(targets, "enemy") && S_SpellIsEnemy(caster, target)) return true;
    if (strstr(targets, "neutral") && target->s.player < MAX_PLAYERS && level.mapinfo &&
        level.mapinfo->players[target->s.player].playerType == kPlayerTypeNeutral) return true;
    return !strstr(targets, "player") && !strstr(targets, "friend") &&
        !strstr(targets, "enemy") && !strstr(targets, "neutral");
}

bool S_SpellAllowsCorpseTarget(uint32_t code, edict_t *caster, edict_t *target) {
    return spell_allows_corpse_target(code, caster, target, false);
}

bool S_SpellAllowsStoredCorpseTarget(uint32_t code, edict_t *caster, edict_t *target) {
    return spell_allows_corpse_target(code, caster, target, true);
}

/* Corpse-consuming spells may target ordinary world corpses or corpses stored
 * in a friendly Meat Wagon. Centralize the storage-aware target rule and the
 * corpse's effective world position; callers still own selection policy. */
bool S_SpellCorpseTargetPosition(uint32_t code, edict_t *caster, edict_t *corpse, vec2_t *position) {
    if (!caster || !corpse) return false;
    if (S_CorpseCargoIsStored(corpse)) {
        edict_t *holder = S_CargoTransportForUnit(corpse);
        if (!holder || !holder->inuse || holder->s.player != caster->s.player ||
            !S_SpellAllowsStoredCorpseTarget(code, caster, corpse)) return false;
    } else if (!S_SpellAllowsCorpseTarget(code, caster, corpse)) {
        return false;
    }
    return !position || S_CorpseCargoPosition(corpse, position);
}

/* Reservation prevents another corpse consumer from claiming the same edict.
 * The status records which ability owns the reservation so release can retire
 * only that owner's marker. */
void S_SpellReserveCorpse(edict_t *corpse, uint32_t code, uint32_t level) {
    if (!corpse || !code) return;
    corpse->aiflags |= AI_CORPSE_RESERVED;
    unit_addstatus(corpse, GetClassName(code), level);
}

void S_SpellReleaseCorpse(edict_t *corpse, uint32_t code) {
    if (!corpse) return;
    corpse->aiflags &= ~AI_CORPSE_RESERVED;
    if (!code) return;
    FOR_LOOP(i, corpse->unit_state ? MAX_UNIT_STATUSES : 0) {
        heroabilitystatus_t *status = corpse->unit_state->statuses + i;
        if (status->level && status->code == code) {
            unit_removestatus(corpse, status, STATUS_REMOVE_SCRIPT);
            G_InvalidateUnitInfoPanel(corpse);
            return;
        }
    }
}

void S_SpellHeal(edict_t *target, float amount) {
    if (!target || amount <= 0) {
        return;
    }
    /* Ethereal units take 66% more spell damage and receive 66% more spell
     * healing, matching the retail Banish interaction with Holy Light. */
    if (unit_hasstatusstate(target, WC3_STATUS_STATE_ETHEREAL)) amount *= 1.66f;
    G_AddHealth(target, amount);
}

void S_SpellCursorSplat(edict_t *clent, float radius) {
    int32_t image = 0;

    if (!clent || !clent->client) {
        return;
    }
    if (isfinite(radius) && radius > 0.0f) {
        edict_t const *caster = G_GetMainSelectedUnit(clent->client);
        cstring_t const race = caster && caster->data.UnitData ? caster->data.UnitData->race : NULL;
        image = gi.ImageIndex(Theme_UnitString(race, "PlacementCursor", DEFAULT_SPELL_AREA_CURSOR));
    } else {
        radius = 0.0f;
    }
    gi.Write(PF_BYTE, &(int32_t){ svc_cursor_splat });
    gi.Write(PF_SHORT, &image);
    gi.Write(PF_FLOAT, &radius);
    gi.unicast(clent);
}

bool S_SpellIsChanneling(edict_t *caster) {
    channel_t *cast = G_GetChannel(caster);
    return caster && cast && cast->code != 0;
}

/* Some temporary summons own a timed lifecycle but are not destroyable by
 * dispel-style summoned-unit damage. Resolve this by the concrete ability
 * procedure so AbilityData aliases inherit the same Animate Dead behavior. */
bool S_SummonIsDispelImmune(edict_t const *unit) {
    abilityitem_t item;
    if (!unit) return false;
    /* Resistant Skin protects exceptional summons from dispel damage, not
     * just direct single-target spell selection. */
    if (G_UnitAbilityLevel(unit, MAKEFOURCC('A','r','s','k')) ||
        G_UnitAbilityLevel(unit, MAKEFOURCC('A','C','r','k')) ||
        G_UnitAbilityLevel(unit, MAKEFOURCC('A','C','s','k'))) return true;
    if (!unit->summon_ability) return false;
    item = S_AbilityItem(unit->summon_ability);
    return item.ability && item.ability->proc == CAbilityAnimateDead;
}

void S_SpellCancelChannel(edict_t *caster) {
    channel_t *cast = G_GetChannel(caster);
    uint32_t code;
    if (!caster || !cast || !cast->code) return;
    code = cast->code;
    cast->code = 0;
    /* Notify the channeled ability so it can strip owned buffs (Mana Flare Bmfl). */
    {
        abilityitem_t item = S_AbilityItem(code);
        abilityCall_t call = MAKE(abilityCall_t, .item = &item);
        if (item.ability) S_AbilityMessage(caster, A_CANCEL, &call);
    }
}

/* Delayed ability helpers that retain edict pointers use channel_t as a save-safe
 * identity carrier even when they are not gameplay channels.  Capture both
 * pointer generations in one place so slot reuse cannot retarget a live thinker. */
edict_t *S_SpellIdentityThinker(edict_t *owner, uint32_t code, edict_t *target) {
    edict_t *ent = G_Spawn();
    if (!ent) return NULL;
    ent->owner = owner; ent->class_id = code; ent->goalentity = target;
    channel_t *cast = G_AddChannel(ent);
    cast->owner_spawn_time = owner ? owner->spawn_time : 0;
    cast->target_spawn_time = target ? target->spawn_time : 0;
    return ent;
}

/* A cast serial and owner incarnation prevent a retired thinker from following a recast or reused edict. */
edict_t *S_SpellChannelThinker(edict_t *caster, uint32_t code) {
    channel_t *cast = G_GetChannel(caster);
    edict_t *ent;
    assert(caster && cast);
    ent = S_SpellIdentityThinker(caster, code, NULL);
    if (!ent) return NULL;
    G_GetChannel(ent)->serial = cast->serial;
    return ent;
}

/* Unit-target channels all need the same target identity snapshot. The owning
 * ability still decides range checks, duration, tick policy and cleanup. */
edict_t *S_SpellChannelTargetThinker(edict_t *caster, uint32_t code, edict_t *target) {
    edict_t *ent = S_SpellChannelThinker(caster, code);
    if (!ent) return NULL;
    ent->goalentity = target;
    G_GetChannel(ent)->target_spawn_time = target ? target->spawn_time : 0;
    return ent;
}

/* Resolve the owner incarnation captured in shared channel storage. True channel
 * casts and save-safe ability helper thinkers both use this identity carrier;
 * liveness, timing and cancellation policy remain with the owning lifecycle. */
edict_t *S_SpellChannelOwner(edict_t const *ent) {
    edict_t *owner;
    channel_t const *cast = G_GetChannel(ent);
    if (!cast || !(owner = ent->owner) || !owner->inuse) return NULL;
    return owner->spawn_time == cast->owner_spawn_time ? owner : NULL;
}

/* Resolve the unit incarnation captured by a target-channel thinker. Liveness,
 * range, relation and completion policy remain with the owning ability. */
edict_t *S_SpellChannelTarget(edict_t const *ent) {
    edict_t *target;
    channel_t const *cast = G_GetChannel(ent);
    if (!cast || !(target = ent->goalentity) || !target->inuse) return NULL;
    return target->spawn_time == cast->target_spawn_time ? target : NULL;
}

/* Each effect rechecks the caster before ticking, independently of edict iteration order. */
bool S_SpellChannelActive(edict_t *ent) {
    edict_t *caster = S_SpellChannelOwner(ent);
    channel_t *cast = G_GetChannel(caster);
    if (!caster) return false;
    spell_run_frame(caster);
    return !M_IsDead(caster) && cast && cast->code == ent->class_id &&
        cast->serial == G_GetChannel(ent)->serial;
}

/* Ending an old thinker must never cancel a replacement order or a newer cast of the same spell. */
void S_SpellEndChannel(edict_t *ent) {
    edict_t *caster = S_SpellChannelOwner(ent);
    channel_t *cast = G_GetChannel(caster);
    if (caster && cast && cast->code == ent->class_id &&
        cast->serial == G_GetChannel(ent)->serial)
        S_SpellCancelChannel(caster);
    G_FreeEdict(ent);
}

/* ---- Unified Spell Pipeline ---- */

/* Per-frame channel enforcement: if the caster has moved from cast_origin,
 * cancel the channel.  Called from G_RunEntity. */
void spell_run_frame(edict_t *ent) {
    channel_t *cast = G_GetChannel(ent);
    if (!cast || !cast->code)
        return;

    /* Stun or death interrupts channel. */
    if (unit_hasstatusstate(ent, WC3_STATUS_STATE_STUNNED) || S_UnitIsSilenced(ent) || M_IsDead(ent)) {
        S_SpellCancelChannel(ent);
        return;
    }

    /* Movement cancel: caster moved from the position where channel began. */
    unsummon_t const *approach = G_GetUnsummon(ent);
    if (!(approach && approach->approaching) &&
        (fabsf(ent->s.origin.x - cast->origin.x) > 0.5f ||
        fabsf(ent->s.origin.y - cast->origin.y) > 0.5f)) {
        S_SpellCancelChannel(ent);
        return;
    }
}

/* Common activation policy used by normal casting and read-only availability
 * queries. Ignoring cooldown never bypasses mana or gameplay disable states. */
static bool spell_activation_check(edict_t *clent, edict_t *caster, uint32_t code,
                                   uint32_t level, bool ignore_cooldown, edict_t *source_item) {
    if (!S_SpellIsAliveTarget(caster) || unit_hasstatusstate(caster, WC3_STATUS_STATE_STUNNED) || S_UnitPolymorphed(caster) ||
        S_UnitIsCycloned(caster)) return false;
    if (S_UnitIsSilenced(caster)) {
        if (clent) G_ShowCommandErrorText(clent, "Silenced.");
        return false;
    }
    uint32_t const cooldown_code = source_item ? G_ItemCooldownCode(source_item, code) : code;
    if (!ignore_cooldown && cooldown_code && !S_SpellCooldownReady(caster, cooldown_code)) {
        if (clent) G_ShowCommandErrorKey(clent, "Cooldown", NULL);
        return false;
    }
    if (!S_SpellCanPay(caster, code, level)) {
        if (clent) G_ShowCommandErrorKey(clent, "Nomana", NULL);
        return false;
    }
    return true;
}

bool S_SpellCanActivate(edict_t *caster, uint32_t code, uint32_t level, bool ignore_cooldown) {
    return spell_activation_check(NULL, caster, code, level, ignore_cooldown, NULL);
}

/* Shared validation for unit-target spells, retaining the normal target/range path. */
static bool spell_validate(edict_t *clent, edict_t *caster, uint32_t code, uint32_t level,
                           edict_t *target, float range, edict_t *source_item) {
    if (!spell_activation_check(clent, caster, code, level, false, source_item)) return false;
    return !(range > 0 && target && !S_SpellTargetInRange(caster, target, range));
}

static bool spell_validate_point(spellPointValidateParams_t const *params) {
    if (!params || !params->caster || !params->point ||
        !spell_activation_check(params->clent, params->caster, params->code, params->level,
                                false, params->source_item)) return false;
    return !(params->range > 0 && Vector2_distance(&params->caster->s.origin2, params->point) > params->range);
}

static void spell_cancel_target_approaches(edict_t *caster, edict_t *except);

/* Start channel: lock caster in place and record the origin for movement-cancel. */
static void spell_begin_channel(edict_t *caster, uint32_t code) {
    channel_t *cast;
    if (caster->stand) caster->stand(caster);
    cast = G_AddChannel(caster);
    cast->serial++;
    cast->code = code;
    cast->origin = caster->s.origin2;
}

/* Pre-execute common work: spend mana, start cooldown, then Mana Flare probes. */
static void spell_commit(edict_t *caster, uint32_t code, uint32_t level,
                         edict_t *active_approach, edict_t *source_item) {
    abilityitem_t committed = S_AbilityItem(code);
    abilityCall_t call = MAKE(abilityCall_t, .item = &committed);

    spell_cancel_target_approaches(caster, active_approach);
    S_SpellCancelChannel(caster);
    S_HumanBreakInvisibility(caster);
    S_PermanentInvisibilityReveal(caster);
    S_UnitStatusAbilityEvent(caster, A_SPELL_COMMIT, &call);
    if (code != MAKEFOURCC('A', 's', 'h', 'm') && code != MAKEFOURCC('A', 'h', 'i', 'd'))
        S_ShadowMeldBreak(caster);
    S_SpellSpendMana(caster, code, level);
    if (!committed.ability || !(committed.ability->flags & AB_COOLDOWN_ON_STATUS_REMOVE)) {
        if (source_item) G_StartItemCooldown(caster, source_item, code, level);
        else S_SpellStartCooldown(caster, code, level);
    }
    S_ManaFlareOnCast(caster, code, level);
}

/* Ability handlers that defer their irreversible effect until an approach
 * succeeds can commit resources at that point instead of when the command is
 * first accepted. */
void S_SpellCommitCast(edict_t *caster, uint32_t code, uint32_t level) {
    if (!caster || !code) return;
    spell_commit(caster, code, level, NULL, NULL);
}

/* Warcraft exposes spell response data only while dispatching the spell event.
 * Publish SPELL_EFFECT at the irreversible cast point: resources have been
 * committed, but the gameplay callback has not run yet. */
static void spell_publish_effect(edict_t *caster, uint32_t code, spellTarget_t target) {
    edict_t *source = target.type == SPELL_TARGET_UNIT ? target.entity : NULL;
    vec2_t const *point = target.type == SPELL_TARGET_POINT ? &target.point : NULL;
    gameEventPointParams_t params = MAKE(gameEventPointParams_t, .edict = caster,
                                         .source = source, .value = (int32_t)code, .point = point);

    params.type = EVENT_PLAYER_UNIT_SPELL_EFFECT;
    G_PublishEventWithPoint(&params);
    params.type = EVENT_UNIT_SPELL_EFFECT;
    G_PublishEventWithPoint(&params);
    /* Keep a per-controller repeat-cast hint. This is transient input state;
     * the normal spell command still validates and executes the next cast. */
    if (caster && caster->inuse && caster->s.number && code) {
        FOR_LOOP(i, game.max_clients) {
            gameClient_t *client = &game.clients[i];
            if (!client->connected || !G_UnitCanControl(client, caster)) continue;
            client->last_spell_caster = caster->s.number;
            client->last_spell_caster_spawn_time = caster->spawn_time;
            client->last_spell_code = code;
        }
    }
}

/* ---- Per-target-type unified callbacks ---- */

static bool spell_item_source_valid(edict_t const *caster, edict_t const *item, uint32_t spawn_time) {
    item_t *rec = G_ItemState(item);
    if (!item) return true;
    return item->inuse && item->spawn_time == spawn_time && rec &&
        !rec->pending_use_removal && rec->carrier == caster;
}

static edict_t const *spell_approach_move_goal(edict_t const *thinker) {
    if (!thinker) return NULL;
    return thinker->goalentity == thinker ? thinker : thinker->goalentity;
}

/* A committed new cast replaces deferred spell casts. Keep the approach that
 * reached range alive until its caller finishes, but retire any older ones. */
static void spell_cancel_target_approaches(edict_t *caster, edict_t *except) {
    bool stop_move = false;

    if (!caster || !caster->inuse) return;
    FILTER_EDICTS(thinker, thinker != except && thinker->inuse &&
                  thinker->owner == caster && thinker->think == S_SpellTargetApproachThink) {
        if (S_SpellChannelOwner(thinker) == caster && move_is_active_order_walk(caster) &&
            caster->goalentity == spell_approach_move_goal(thinker)) {
            caster->goalentity = NULL;
            stop_move = true;
        }
        G_FreeEdict(thinker);
    }
    if (stop_move) unit_stand(caster);
}

/* Commit a validated unit-target spell at the point where its cast range is reached. */
static bool spell_execute_unit_target(spellUnitTargetParams_t const *params, edict_t *active_approach) {
    spellTarget_t st = { .type = SPELL_TARGET_UNIT, .entity = params->target };
    abilityitem_t item = { .code = params->code, .ability = params->spell };
    abilityCall_t call = MAKE(abilityCall_t, .item = &item, .target = &st,
                              .source_item = params->source_item,
                              .source_item_spawn_time = params->source_item_spawn_time);

    spell_commit(params->caster, params->code, params->level, active_approach, params->source_item);
    bool const blocked = !S_SpellShieldImpactDeferred(params->code) &&
        S_TryBlockSpellShield(params->caster, params->code, params->target);
    if (!blocked && (params->spell->flags & AB_CHANNEL))
        spell_begin_channel(params->caster, params->code);
    if (!blocked) spell_publish_effect(params->caster, params->code, st);
    bool const executed = blocked || S_AbilityMessage(params->caster, A_EXECUTE, &call);
    if (executed && params->source_item) G_CompleteItemUse(params->caster, params->source_item);
    /* Existing unit spells historically accepted the order once validation and
     * commit succeeded even if a handler returned false from A_EXECUTE. Item
     * casts additionally need a positive execution result before consuming. */
    return params->source_item ? executed : true;
}

/* Commit a validated point-target spell, optionally completing its carried
 * item only after the ability has successfully executed. */
static bool spell_execute_point_target(edict_t *clent, edict_t *caster, uint32_t code,
                                       uint32_t level, ability_t const *spell,
                                       vec2_t const *point, edict_t *entity_target,
                                       edict_t *source_item, uint32_t source_item_spawn_time,
                                       edict_t *active_approach) {
    spellTarget_t st = (entity_target && spell_is_tree_interaction(spell)) ?
                       MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = entity_target) :
                       MAKE(spellTarget_t, .type = SPELL_TARGET_POINT, .point = *point);
    abilityitem_t item = { .code = code, .ability = spell };
    abilityCall_t call = MAKE(abilityCall_t, .item = &item, .target = &st,
                              .source_item = source_item,
                              .source_item_spawn_time = source_item_spawn_time);

    spell_commit(caster, code, level, active_approach, source_item);
    if (spell->flags & AB_CHANNEL)
        spell_begin_channel(caster, code);
    spell_publish_effect(caster, code, st);
    bool const executed = S_AbilityMessage(caster, A_EXECUTE, &call);
    if (executed && source_item) {
        G_CompleteItemUse(caster, source_item);
        if (clent && clent->client && clent->client->menu.ability_item == source_item &&
            clent->client->menu.ability_item_spawn_time == source_item_spawn_time) {
            clent->client->menu.ability_item = NULL;
            clent->client->menu.ability_item_spawn_time = 0;
        }
    }
    return source_item ? executed : true;
}

/* Ranged unit and point spells are accepted before the caster is in range.
 * Warsmash's CBehaviorTargetSpellBase owns that approach phase and only performs
 * the spell effect once canReach(target, castRange) becomes true. The selected
 * unit or point remains authoritative while this thinker watches the ordinary
 * Move order; replacing that order cancels the pending cast. */
void S_SpellTargetApproachThink(edict_t *thinker) {
    channel_t *thinker_cast = G_GetChannel(thinker);
    edict_t *caster = S_SpellChannelOwner(thinker);
    edict_t *move_goal = thinker ? thinker->goalentity : NULL;
    uint32_t code = thinker ? thinker->class_id : 0;
    ability_t const *spell = S_SpellAbilityForCode(code);
    bool const tree_approach = spell_is_tree_interaction(spell) &&
        move_goal == thinker;
    edict_t *target = tree_approach ? thinker->build : move_goal;
    abilityitem_t item = { .code = code, .ability = spell };
    edict_t *source_item = thinker && thinker_cast ? thinker_cast->source_item : NULL;
    bool const point_target = spell && move_goal == thinker && !tree_approach &&
        (spell->target_type == SPELL_TARGET_POINT || spell->target_type == SPELL_TARGET_UNIT_OR_POINT);
    /* Ability-owned entity cursors retain the entity and incarnation while
     * approaching, rather than freezing a point at the original click. */
    bool const entity_point_target = spell && !tree_approach && move_goal != thinker &&
        spell->target_type == SPELL_TARGET_POINT;
    uint32_t level;
    float range;
    spellTarget_t st;

    if (!caster || M_IsDead(caster) || !target || !spell ||
        !spell_item_source_valid(caster, source_item, thinker_cast->source_item_spawn_time) ||
        (!point_target && !entity_point_target && !tree_approach && spell->target_type != SPELL_TARGET_UNIT &&
         spell->target_type != SPELL_TARGET_UNIT_OR_POINT)) {
        if (caster && caster->goalentity == move_goal) {
            caster->goalentity = NULL;
            unit_stand(caster);
        }
        G_FreeEdict(thinker);
        return;
    }
    if (tree_approach && (!thinker_cast || !target || !target->inuse ||
                               target->spawn_time != thinker_cast->target_spawn_time)) {
        G_FreeEdict(thinker);
        return;
    }
    if (!point_target && !tree_approach && S_SpellChannelTarget(thinker) != target) {
        if (caster->goalentity == target && move_is_active_order_walk(caster)) {
            caster->goalentity = NULL;
            unit_stand(caster);
        }
        G_FreeEdict(thinker);
        return;
    }
    /* A replacement order is authoritative. If the same spell-owned Move is
     * still active but its target died/disappeared, terminate that approach too. */
    /* A tree is an occupied destructable, so the Giant's Move can naturally
     * finish at its collision edge and switch to Stand before the next spell
     * approach tick. Keep the pending Grab Tree order alive in that case and
     * evaluate its collision-adjusted range below. A different goal still
     * means the player replaced the order. */
    if (caster->goalentity != move_goal ||
        (!move_is_active_order_walk(caster) && !tree_approach)) {
        G_FreeEdict(thinker);
        return;
    }
    level = S_SpellLevel(caster, code);
    /* Force of Nature has no custom approach-range override. */
    range = code == MAKEFOURCC('A','E','f','n') ? S_SpellRange(code, level) :
            spell_approach_range(code, level, spell);
    if (spell->proc == CAbilityEatTree)
        range = spell_tree_target_range(code, level, caster);
    if (point_target || entity_point_target || tree_approach) {
        edict_t *clent = G_GetPlayerEntityByNumber(caster->s.player);
        vec2_t const *validate_point = tree_approach ? &target->s.origin2 : &move_goal->s.origin2;
        spellPointValidateParams_t val = MAKE(spellPointValidateParams_t,
                                              .clent = clent, .caster = caster, .code = code,
                                              .level = level, .point = validate_point,
                                              .range = 0.0f, .source_item = source_item);
        /* The ability owns entity eligibility, including destructables which
         * ordinary unit-target validation intentionally rejects. */
        st = (entity_point_target || tree_approach) ?
             MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target) :
             MAKE(spellTarget_t, .type = SPELL_TARGET_POINT, .point = move_goal->s.origin2);
        if ((spell_is_tree_interaction(spell) && !G_UnitAbilityLevel(caster, code)) ||
            (!tree_approach && !spell_validate_point(&val)) ||
            (spell->proc == CAbilityEatTree && !S_SpellAllowsTarget(code, caster, target)) ||
            !spell_message(caster, A_VALIDATE, &item, &st)) {
            if (caster->goalentity == move_goal) caster->goalentity = NULL;
            unit_stand(caster);
            G_FreeEdict(thinker);
            return;
        }
        /* A large Giant need only reach a tree's edge rather than overlap
         * the destructable's centre to cast Grab Tree. */
        if (spell_is_tree_interaction(spell))
            range = spell_tree_target_range(code, level, caster);
        float const distance = spell_is_tree_interaction(spell) ?
            spell_tree_distance(caster, target) :
            Vector2_distance(&caster->s.origin2, &target->s.origin2);
        if (range > 0.0f && distance > range)
            return;
        if (caster->goalentity == move_goal) caster->goalentity = NULL;
        unit_stand(caster);
        spell_execute_point_target(clent, caster, code, level, spell, validate_point,
                                   (tree_approach || entity_point_target) ? target : NULL,
                                   source_item, thinker_cast->source_item_spawn_time, thinker);
        G_FreeEdict(thinker);
        return;
    }
    st = MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target);

    if (!S_SpellAllowsTarget(code, caster, target) || !spell_message(caster, A_VALIDATE, &item, &st)) {
        unit_stand(caster);
        G_FreeEdict(thinker);
        return;
    }
    if (!S_SpellTargetInRange(caster, target, range))
        return;

    /* Mana/cooldown can change while walking. Do not spend or fire the ability
     * unless it is still legal at the actual cast point. */
    if (!spell_validate(NULL, caster, code, level, target, range, source_item)) {
        unit_stand(caster);
        G_FreeEdict(thinker);
        return;
    }

    spellUnitTargetParams_t params = {
        .caster = caster, .code = code, .level = level, .spell = spell, .target = target,
        .source_item = source_item, .source_item_spawn_time = thinker_cast->source_item_spawn_time
    };
    unit_stand(caster);
    spell_execute_unit_target(&params, thinker);
    G_FreeEdict(thinker);
}

/* Start the ordinary walk order used to bring an out-of-range spell target into range. */
static bool spell_begin_target_approach(edict_t *caster, uint32_t code, edict_t *target,
                                        vec2_t const *point, edict_t *source_item,
                                        uint32_t source_item_spawn_time) {
    edict_t *thinker, *goal;
    vec2_t approach;

    if (!caster || (!target && !point) || (target && point) ||
        !S_UnitCanTranslate(caster) || S_GoldMineWorkerIsInside(caster))
        return false;

    thinker = G_Spawn();
    if (!thinker) return false;
    goal = point ? thinker : target;
    if (point) thinker->s.origin2 = *point;
    /* Tree pathing blocks movement to the target center. Route War Club to a
     * reachable cell within its authored cast range instead, while the cast
     * thinker retains the tree as its validated ability target. */
    ability_t const *spell = S_SpellAbilityForCode(code);
    if (target && spell_is_tree_interaction(spell)) {
        uint32_t const level = S_SpellLevel(caster, code);
        float const range = spell_tree_target_range(code, level, caster);
        bool found = CM_ClosestReachablePointForRadiusFlags(
            &caster->s.origin2, &target->s.origin2, caster->collision,
            M_UnitStaticPathingFlags(caster), &approach) &&
            spell_tree_distance_from_point(target, &approach,
                M_UnitStaticPathingFlags(caster)) <= range;
        /* If the path map cannot provide an interaction cell (for example
         * during a map transition), retain the ordinary entity Move order.
         * Rejecting the cast here leaves the command cursor consumed without
         * giving the player the expected walk response. */
        if (found) {
            goal = thinker;
            thinker->build = target;
            thinker->s.origin2 = approach;
        }
    }
    thinker->owner = caster;
    thinker->goalentity = goal;
    channel_t *cast = G_AddChannel(thinker);
    cast->owner_spawn_time = caster->spawn_time;
    cast->target_spawn_time = target ? target->spawn_time : 0;
    thinker->class_id = code;
    cast->source_item = source_item;
    cast->source_item_spawn_time = source_item_spawn_time;
    thinker->think = S_SpellTargetApproachThink;
    thinker->freetime = G_Time();
    /* Replace the old pending cast before installing a new Move order. A unit
     * target pointer alone cannot distinguish two casts aimed at that same unit. */
    spell_cancel_target_approaches(caster, thinker);
    order_move(caster, goal);
    if (caster->goalentity != goal || !move_is_active_order_walk(caster)) {
        G_FreeEdict(thinker);
        return false;
    }
    return true;
}

/* Command-card casts bypass G_IssueUnit*Order, so report the accepted spell
 * order to innate owners such as Stop's guard point the same way. */
static void spell_order_accepted(edict_t *caster, ability_t const *spell) {
    S_UnitAbilityOrderAccepted(caster, spell->orders && *spell->orders ? *spell->orders : spell->classname);
}

/* Called when user clicks a target entity for a UNIT-target spell. */
static bool spell_unit_target_selected(edict_t *clent, edict_t *target) {
    edict_t *caster = G_GetMainSelectedUnit(clent->client);
    uint32_t code = S_SpellCurrentCode(clent, 0);
    uint32_t level = S_SpellLevel(caster, code);
    ability_t const *spell = S_SpellAbilityForCode(code);
    float range = spell_approach_range(code, level, spell);
    abilityitem_t item = { .code = code, .ability = spell };
    edict_t *source_item = clent->client->menu.ability_item;
    uint32_t source_item_spawn_time = clent->client->menu.ability_item_spawn_time;
    spellTarget_t st = { .type = SPELL_TARGET_UNIT, .entity = target };

    if (!spell || !spell_item_source_valid(caster, source_item, source_item_spawn_time)) {
        return false;
    }
    if (spell->proc == CAbilityEatTree)
        range = spell_tree_target_range(code, level, caster);
    /* Searing Arrows and Orb of Annihilation are attack modifiers, not
     * unit-target spell casts. Their
     * manual target order arms one projectile and spends mana at weapon
     * damage point, so it must bypass cast-time mana/cooldown commit. */
    if (spell->proc == CAbilityFlamingArrows || spell->proc == CAbilityColdArrows ||
        spell->proc == CAbilityBlackArrow) {
        bool const accepted = spell->proc == CAbilityBlackArrow
            ? S_BlackArrowIssueOrder(caster, target, code) :
            spell->proc == CAbilityColdArrows
            ? S_ColdArrowsIssueOrder(caster, target, code)
            : S_SearingArrowsIssueOrder(caster, target, code);
        if (accepted) spell_order_accepted(caster, spell);
        return accepted;
    }
    /* Range is intentionally excluded here. An otherwise valid out-of-range
     * target is an accepted order; the caster must walk into cast range. */
    if (!spell_validate(clent, caster, code, level, target, 0.0f, source_item)) {
        return false;
    }
    if (!S_SpellAllowsTarget(code, caster, target)) {
        return false;
    }
    if (!spell_message(caster, A_VALIDATE, &item, &st)) {
        return false;
    }

    if (!S_SpellTargetInRange(caster, target, range)) {
        if (!spell_begin_target_approach(caster, code, target, NULL, source_item, source_item_spawn_time)) {
            return false;
        }
        spell_order_accepted(caster, spell);
        return true;
    }

    spellUnitTargetParams_t params = {
        .caster = caster, .code = code, .level = level, .spell = spell, .target = target,
        .source_item = source_item, .source_item_spawn_time = source_item_spawn_time
    };
    if (!spell_execute_unit_target(&params, NULL)) return false;
    spell_order_accepted(caster, spell);
    return true;
}

/* Build Tiny uses the same game-authored cursor entity as ordinary worker
 * placement.  The generic client already renders its snapped model and
 * red/green pathing footprint.  Only the game chooses which building it is. */
static bool spell_is_tiny_structure(ability_t const *spell) {
    return spell && spell->proc == CAbilityTinyStructure;
}

static void spell_tiny_cursor_clear(edict_t *clent) {
    entityState_t empty = {0};
    if (!clent || !clent->client) return;
    gi.Write(PF_BYTE, &(int32_t){svc_cursor});
    gi.Write(PF_ENTITY, &empty);
    gi.unicast(clent);
}

static bool spell_tiny_cursor_show(edict_t *clent, edict_t *caster, uint32_t code) {
    entityState_t cursor;
    gameClient_t *owner;
    uint32_t building;
    if (!clent || !clent->client || !caster) return false;
    building = S_TinyStructureUnitId(caster, code, MAX(1u, S_SpellLevel(caster, code)));
    owner = G_GetPlayerClientByNumber(caster->s.player);
    if (!building || !G_UnitIsBuilding(building) || !owner) return false;
    FillUnitData(&cursor, building, "stand");
    cursor.player = caster->s.player;
    G_SetEntityTeamColor(&cursor, owner->ps.color);
    cursor.pathing_preview = EntityPathingPreviewPack(
        caster->s.number,
        EntityPathingPreviewPrevented(cursor.pathing_preview),
        EntityPathingPreviewRequired(cursor.pathing_preview));
    gi.Write(PF_BYTE, &(int32_t){svc_cursor});
    gi.Write(PF_ENTITY, &cursor);
    gi.unicast(clent);
    return true;
}

/* Point execution may be reached through an ability-owned entity cursor.
 * Keep that target for Move and validation until the actual cast. */
static bool spell_point_or_entity_target_selected(edict_t *clent, vec2_t const *point, edict_t *target) {
    if (!clent || !clent->client || !point) {
        return false;
    }
    edict_t *caster = G_GetMainSelectedUnit(clent->client);
    uint32_t code = S_SpellCurrentCode(clent, 0);
    uint32_t level;
    ability_t const *spell;
    float range;
    abilityitem_t item;
    edict_t *source_item;
    uint32_t source_item_spawn_time;
    spellPointValidateParams_t val;
    if (!caster) {
        return false;
    }
    level = S_SpellLevel(caster, code);
    spell = S_SpellAbilityForCode(code);
    /* Force of Nature has no custom approach-range override. Avoid invoking
     * its ability callback with a NULL caster from this entity-click path. */
    range = code == MAKEFOURCC('A','E','f','n') ? S_SpellRange(code, level) :
            spell_approach_range(code, level, spell);
    item = MAKE(abilityitem_t, .code = code, .ability = spell);
    source_item = clent->client->menu.ability_item;
    source_item_spawn_time = clent->client->menu.ability_item_spawn_time;
    val = MAKE(spellPointValidateParams_t,
               .clent = clent, .caster = caster, .code = code, .level = level,
               .point = point, .range = 0.0f, .source_item = source_item);

    if (!spell || !spell_item_source_valid(caster, source_item, source_item_spawn_time)) {
        return false;
    }
    if (!spell_validate_point(&val)) {
        return false;
    }
    spellTarget_t st = target ? MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target) :
                                MAKE(spellTarget_t, .type = SPELL_TARGET_POINT, .point = *point);
    if (!spell_message(caster, A_VALIDATE, &item, &st)) {
        return false;
    }

    /* War Club uses the edge of the large caster and the tree footprint. */
    if (target && spell_is_tree_interaction(spell))
        range += MAX(0.0f, caster->collision) + MAX(0.0f, target->collision);
    if (range > 0.0f && Vector2_distance(&caster->s.origin2, point) > range) {
        if (!spell_begin_target_approach(caster, code, target, target ? NULL : point,
                                         source_item, source_item_spawn_time)) {
            return false;
        }
        spell_order_accepted(caster, spell);
        if (spell_is_tiny_structure(spell)) spell_tiny_cursor_clear(clent);
        else S_SpellCursorSplat(clent, 0.0f);
        G_SendPointConfirmation(clent, point, false);
        return true;
    }

    if (!spell_execute_point_target(clent, caster, code, level, spell, point,
                                    (spell->proc == CAbilityGrabTree) ? target : NULL,
                                    source_item, source_item_spawn_time, NULL)) return false;
    spell_order_accepted(caster, spell);
    if (spell_is_tiny_structure(spell)) spell_tiny_cursor_clear(clent);
    else S_SpellCursorSplat(clent, 0.0f);
    G_SendPointConfirmation(clent, point, false);
    return true;
}

/* Called when user clicks a location for a POINT-target spell. */
static bool spell_point_target_selected(edict_t *clent, vec2_t const *point) {
    return spell_point_or_entity_target_selected(clent, point, NULL);
}

static bool spell_ability_entity_target_selected(edict_t *clent, edict_t *target) {
    if (!clent || !clent->client || !target || !target->inuse) return false;
    if (!S_SpellEntityTargetMode(clent)) {
        return false;
    }
    return spell_point_or_entity_target_selected(clent, &target->s.origin2, target);
}

/* Entity hits arrive as Select commands from the universal client. While a
 * spell's point cursor is active, treat those hits as point casts at the
 * entity's ground position (retail accepts clicks on pickups for Blink). */
bool S_SpellPointTargetMode(edict_t *clent) {
    return clent && clent->client && !clent->client->menu.on_entity_selected &&
           clent->client->menu.on_location_selected == spell_point_target_selected;
}

bool S_SpellEntityTargetMode(edict_t *clent) {
    ability_t const *spell;
    abilityitem_t item;
    abilityCall_t call;
    if (!clent || !clent->client) return false;
    spell = S_SpellAbilityForCode(S_SpellCurrentCode(clent, 0));
    if (!spell) return false;
    item = MAKE(abilityitem_t, .code = S_SpellCurrentCode(clent, 0), .ability = spell);
    call = MAKE(abilityCall_t, .item = &item);
    spell->proc(G_GetMainSelectedUnit(clent->client), A_SPELL_TARGET_MODE, &call);
    return call.spell_entity_target_mode;
}

bool S_SpellEntityTarget(edict_t *clent, edict_t *target) {
    return clent && clent->client && target && target->inuse &&
           (clent->client->menu.on_entity_selected ?
             clent->client->menu.on_entity_selected(clent, target) :
             spell_ability_entity_target_selected(clent, target));
}

static void spell_target_mode_exit(edict_t *clent) {
    abilityitem_t item = S_AbilityItem(S_SpellCurrentCode(clent, 0));
    abilityCall_t call = MAKE(abilityCall_t, .item = &item, .entity = clent);
    if (item.ability) S_AbilityMessage(G_GetMainSelectedUnit(clent->client), A_TARGET_MODE_EXIT, &call);
}

/* The generic command/HUD paths can clear target selection without knowing
 * which ability owns its cursor presentation. Dispatch cleanup through the
 * active ability while its target callback is still available. */
void S_AbilityTargetModeExit(edict_t *clent) {
    if (!clent || !clent->client ||
        (!clent->client->menu.on_entity_selected && !clent->client->menu.on_location_selected)) return;
    spell_target_mode_exit(clent);
}

bool S_SpellPointTargetEntity(edict_t *clent, edict_t *target) {
    if (!S_SpellPointTargetMode(clent) || !target || !target->inuse) return false;
    return spell_point_target_selected(clent, &target->s.origin2);
}

/* No-target (self-cast / instant) execute in-place. */
static void spell_no_target_execute(edict_t *clent) {
    edict_t *caster = G_GetMainSelectedUnit(clent->client);
    uint32_t code = S_SpellCurrentCode(clent, 0);
    uint32_t level = S_SpellLevel(caster, code);
    ability_t const *spell = S_SpellAbilityForCode(code);
    abilityitem_t item = { .code = code, .ability = spell };
    edict_t *source_item = clent->client->menu.ability_item;
    uint32_t source_item_spawn_time = clent->client->menu.ability_item_spawn_time;

    if (!spell || !spell_item_source_valid(caster, source_item, source_item_spawn_time)) return;
    if (!spell_validate(clent, caster, code, level, NULL, 0.0f, source_item)) return;
    spellTarget_t st = { .type = SPELL_TARGET_NONE, .entity = NULL };
    if (!spell_message(caster, A_VALIDATE, &item, &st)) return;

    /* Couple Instant can require an approach. Its handler commits only after
     * the rider has been created successfully. */
    if (spell->proc != CAbilityCoupleInstant)
        spell_commit(caster, code, level, NULL, source_item);
    if (spell->flags & AB_CHANNEL) spell_begin_channel(caster, code);
    spell_publish_effect(caster, code, st);
    if (spell_message(caster, A_EXECUTE, &item, &st) && source_item)
        G_CompleteItemUse(caster, source_item);
}

bool S_CastNoTargetSpell(edict_t *caster, uint32_t code) {
    uint32_t level;
    ability_t const *spell;
    spellTarget_t target = { .type = SPELL_TARGET_NONE };

    if (!caster || !code || !G_UnitAbilityLevel(caster, code) || S_UnitPolymorphed(caster)) {
        if (WC3_MISHA_DEBUG_ENABLED() && code == MAKEFOURCC('A','r','s','g'))
            fprintf(stderr, "WC3_MISHA cast-rejected ability=Arsg reason=missing_caster_ability_or_polymorph caster=%u level=%u polymorphed=%u\n",
                    caster ? (unsigned)caster->s.number : 0u,
                    caster ? G_UnitAbilityLevel(caster, code) : 0u,
                    caster ? (unsigned)S_UnitPolymorphed(caster) : 0u);
        return false;
    }
    spell = S_SpellAbilityForCode(code);
    abilityitem_t item = { .code = code, .ability = spell };
    if (!spell || spell->target_type != SPELL_TARGET_NONE || !S_AbilityHasCommand(spell)) {
        if (WC3_MISHA_DEBUG_ENABLED() && code == MAKEFOURCC('A','r','s','g'))
            fprintf(stderr, "WC3_MISHA cast-rejected ability=Arsg reason=ability_shape spell=%p target_type=%u command=%u\n",
                    (void *)spell, spell ? (unsigned)spell->target_type : 0u,
                    (unsigned)(spell && S_AbilityHasCommand(spell)));
        return false;
    }
    /* A running Mana Flare is a cancellable stance, not a new cast. It must
     * remain possible to turn it off while the original cooldown is active
     * or current mana is insufficient. Do not publish a spell event, break
     * channels or run spell_commit for the deactivation. */
    if (spell->proc == CAbilityManaFlare && S_ManaFlareActive(caster)) {
        if (!S_SpellIsAliveTarget(caster)) return false;
        return spell_message(caster, A_EXECUTE, &item, &target) != 0;
    }
    level = S_SpellLevel(caster, code);
    if (!spell_validate(NULL, caster, code, level, NULL, 0, NULL)) {
        if (WC3_MISHA_DEBUG_ENABLED() && code == MAKEFOURCC('A','r','s','g'))
            fprintf(stderr, "WC3_MISHA cast-rejected ability=Arsg reason=spell_validate caster=%u alive=%u stunned=%u silenced=%u cooldown=%u mana_ok=%u mana=%.1f/%.1f cost=%.1f\n",
                    (unsigned)caster->s.number, (unsigned)S_SpellIsAliveTarget(caster),
                    (unsigned)unit_hasstatusstate(caster, WC3_STATUS_STATE_STUNNED), (unsigned)S_UnitIsSilenced(caster),
                    (unsigned)S_SpellCooldownReady(caster, code),
                    (unsigned)S_SpellCanPay(caster, code, level), caster->mana.value,
                    caster->mana.max_value, S_SpellNumber(code, ABILITY_NUMBER_COST, level));
        return false;
    }
    if (!spell_message(caster, A_VALIDATE, &item, &target)) {
        if (WC3_MISHA_DEBUG_ENABLED() && code == MAKEFOURCC('A','r','s','g'))
            fprintf(stderr, "WC3_MISHA cast-rejected ability=Arsg reason=ability_validate caster=%u\n",
                    (unsigned)caster->s.number);
        return false;
    }

    if (WC3_MISHA_DEBUG_ENABLED() && code == MAKEFOURCC('A','r','s','g'))
        fprintf(stderr, "WC3_MISHA cast-accepted ability=Arsg caster=%u level=%u mana_before=%.1f cost=%.1f cooldown_ready=%u\n",
                (unsigned)caster->s.number, level, caster->mana.value,
                S_SpellNumber(code, ABILITY_NUMBER_COST, level), (unsigned)S_SpellCooldownReady(caster, code));

    spell_commit(caster, code, level, NULL, NULL);
    if (spell->flags & AB_CHANNEL) {
        spell_begin_channel(caster, code);
    }
    spell_publish_effect(caster, code, target);
    spell_message(caster, A_EXECUTE, &item, &target);
    return true;
}

bool S_CastPointTargetSpell(edict_t *caster, uint32_t code, vec2_t const *point) {
    uint32_t level;
    float range;
    ability_t const *spell;
    spellTarget_t target;

    if (!caster || !point || !code || !G_UnitAbilityLevel(caster, code) || S_UnitPolymorphed(caster)) return false;
    spell = S_SpellAbilityForCode(code);
    abilityitem_t item = { .code = code, .ability = spell };
    if (!spell || (spell->target_type != SPELL_TARGET_POINT &&
                   spell->target_type != SPELL_TARGET_UNIT_OR_POINT) ||
        !S_AbilityHasCommand(spell) || (spell->flags & AB_TOGGLE)) return false;
    level = S_SpellLevel(caster, code);
    range = S_SpellRange(code, level);
    spellPointValidateParams_t val = MAKE(spellPointValidateParams_t,
                                          .caster = caster, .code = code, .level = level,
                                          .point = point, .range = range, .source_item = NULL);
    if (!spell_validate_point(&val)) return false;
    target = MAKE(spellTarget_t, .type = SPELL_TARGET_POINT, .point = *point);
    if (!spell_message(caster, A_VALIDATE, &item, &target)) return false;

    spell_commit(caster, code, level, NULL, NULL);
    if (spell->flags & AB_CHANNEL) spell_begin_channel(caster, code);
    spell_publish_effect(caster, code, target);
    spell_message(caster, A_EXECUTE, &item, &target);
    return true;
}

/* Autocast and AI orders use the same target and resource contract as a player-selected unit spell. */
bool S_CastUnitTargetSpell(edict_t *caster, uint32_t code, edict_t *unit) {
    uint32_t level;
    ability_t const *spell;
    spellTarget_t target = { .type = SPELL_TARGET_UNIT, .entity = unit };

    if (!caster || !unit || !code) return false;
    if (!G_UnitAbilityLevel(caster, code)) return false;
    if (S_UnitPolymorphed(caster)) return false;
    spell = S_SpellAbilityForCode(code);
    abilityitem_t item = { .code = code, .ability = spell };
    if (!spell) return false;
    if (spell->target_type != SPELL_TARGET_UNIT) return false;
    if (!S_AbilityHasCommand(spell)) return false;
    level = S_SpellLevel(caster, code);
    if (!spell_validate(NULL, caster, code, level, unit, S_SpellRange(code, level), NULL)) return false;
    if (!S_SpellAllowsTarget(code, caster, unit)) return false;
    if (!spell_message(caster, A_VALIDATE, &item, &target)) return false;

    spell_commit(caster, code, level, NULL, NULL);
    if (!S_SpellShieldImpactDeferred(code) && S_TryBlockSpellShield(caster, code, unit)) return true;
    if (spell->flags & AB_CHANNEL) spell_begin_channel(caster, code);
    spell_publish_effect(caster, code, target);
    spell_message(caster, A_EXECUTE, &item, &target);
    return true;
}

/* Resolve the applying entity incarnation carried by a timed status.  Status
 * procedures own expiry/damage policy; this helper only centralizes the
 * persistent source pointer plus source_spawn_time identity contract. */
edict_t *S_SpellStatusSource(heroabilitystatus_t const *slot) {
    edict_t *source;
    if (!slot || !(source = slot->source) || !source->inuse) return NULL;
    return source->spawn_time == slot->source_spawn_time ? source : NULL;
}

/* Shared nearest-unit acquisition for ordinary unit-target autocast abilities.
 * Ability-specific procedures still own relation and wounded-only policy; this
 * helper centralizes authored range, target masks, nearest-target selection and
 * the normal cast path. */
bool S_AutocastAcquireUnit(edict_t *caster, uint32_t code, bool friendly, bool wounded, float fallback_range) {
    edict_t *best = NULL;
    float best_distance = FLT_MAX;
    float range;
    abilityitem_t item;

    if (!caster || !code) return false;
    item = S_AbilityItem(code);
    if (!item.ability) return false;
    range = S_SpellRange(code, S_SpellLevel(caster, code));
    if (range <= 0.0f) range = fallback_range;
    FILTER_EDICTS(target, target != caster && S_SpellIsAliveTarget(target)) {
        float distance;
        if (friendly != S_SpellIsFriend(caster, target)) continue;
        if (wounded && target->health.value >= target->health.max_value) continue;
        if (!S_SpellAllowsTarget(code, caster, target)) continue;
        /* Filter through the ability's ordinary validation before choosing
         * the nearest candidate. A nearer unit may already carry this buff or
         * otherwise fail application while a farther valid target is available. */
        spellTarget_t spell_target = MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target);
        abilityCall_t call = MAKE(abilityCall_t, .item = &item, .target = &spell_target);
        if (!S_AbilityMessage(caster, A_VALIDATE, &call)) continue;
        distance = Vector2_distance(&target->s.origin2, &caster->s.origin2);
        if ((range <= 0.0f || distance <= range) && distance < best_distance) {
            best = target;
            best_distance = distance;
        }
    }
    return best && S_CastUnitTargetSpell(caster, code, best);
}

/* Common homing spell-missile launch contract.  The ability owns payload and
 * impact policy; this helper owns authored MissileArt, source presentation,
 * target identity, movement, and projectile presentation lifecycle. */
edict_t *S_SpawnUnitTargetSpellMissile(edict_t *caster, uint32_t code, edict_t *target, float speed, umove_t *move) {
    cstring_t art;
    edict_t *missile;

    if (!caster || !target || !code || !move) return NULL;
    missile = G_Spawn();
    if (!missile) return NULL;
    art = G_AbilityEffectArt(code, WC3_EFFECT_MISSILE, 0);
    missile->class_id = code;
    missile->s.origin = caster->s.origin;
    missile->s.angle = caster->s.angle;
    missile->s.model = art ? G_RegisterModel(art) : 0;
    missile->s.player = caster->s.player;
    G_InheritUnitTeamColor(missile, caster);
    missile->goalentity = target;
    channel_t *cast = G_AddChannel(missile);
    cast->owner_spawn_time = caster->spawn_time;
    cast->target_spawn_time = target->spawn_time;
    missile->owner = caster;
    missile->velocity = MAX(0.0f, speed) / 1000.0f;
    missile->movetype = MOVETYPE_FLYMISSILE;
    missile->currentmove = move;
    G_StartProjectilePresentation(missile);
    return missile;
}

/* Commit an already-resolved spell relocation without choosing presentation.
 * Destination search/failure policy and source/destination art remain with the
 * owning ability; this helper owns the authoritative position/FOW/event step. */
void S_SpellCommitRelocation(edict_t *unit, vec2_t const *position) {
    vec2_t old_position;

    if (!unit || !position) return;
    old_position = unit->s.origin2;
    unit->s.origin2 = *position;
    unit->s.origin.x = position->x;
    unit->s.origin.y = position->y;
    if (unit->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    gi.LinkEntity(unit);
    G_UnitPositionChanged(unit, &old_position);
}

/* Standard relocation presentation used by Mass Teleport/Way Gate: SpecialArt
 * at both ends layered over the presentation-neutral commit primitive. */
void S_SpellRelocateUnit(edict_t *unit, uint32_t code, vec2_t const *position) {
    vec2_t source;

    if (!unit || !position) return;
    source = unit->s.origin2;
    /* An explicit spell teleport ends the old caster-owned swarm; normal
     * movement and raw script repositioning are not treated as teleports. */
    if (Vector2_distance(&source, position) > 0.001f)
        S_LocustSwarmCancelForCaster(unit);
    G_SpawnAbilityEffectAtPoint(code, WC3_EFFECT_SPECIAL, 0, &source, true);
    S_SpellCommitRelocation(unit, position);
    G_SpawnAbilityEffectAtPoint(code, WC3_EFFECT_SPECIAL, 0, &unit->s.origin2, true);
}

/* Resolve the original caster incarnation carried by a homing spell missile.
 * This mirrors target-incarnation tracking so a recycled owner edict cannot be
 * attributed an older projectile impact. */
edict_t *S_SpellProjectileOwner(edict_t *missile) {
    edict_t *owner;
    channel_t const *cast = G_GetChannel(missile);
    if (!cast || !(owner = missile->owner) || !owner->inuse) return NULL;
    return owner->spawn_time == cast->owner_spawn_time ? owner : NULL;
}

/* Resolve the original unit incarnation carried by a homing spell missile.
 * Callers still decide whether caster death, relation changes, or other impact
 * conditions invalidate their ability-specific effect. */
edict_t *S_SpellProjectileTarget(edict_t *missile) {
    edict_t *target;
    channel_t const *cast = G_GetChannel(missile);
    if (!cast || !(target = missile->goalentity) || !target->inuse) return NULL;
    return target->spawn_time == cast->target_spawn_time ? target : NULL;
}

/* Shared eligibility for chained/bouncing unit spells. Selection order,
 * timing, visited-state storage, and damage/heal progression remain with the
 * owning ability. */
bool S_SpellBounceTargetAllowed(edict_t *caster, uint32_t code, edict_t const *from, edict_t *target,
                                float radius, bool friendly) {
    if (!caster || !from || !S_SpellIsAliveTarget(target)) return false;
    if (friendly != S_SpellIsFriend(caster, target)) return false;
    if (!S_SpellAllowsTarget(code, caster, target)) return false;
    return radius <= 0.0f || Vector2_distance(&target->s.origin2, &from->s.origin2) <= radius;
}

/* Synchronous bounce families keep a small local visited array. Asynchronous
 * save-safe chains may use persistent marker edicts instead. */
bool S_SpellTargetVisited(edict_t *const *visited, uint32_t count, edict_t const *target) {
    if (!visited || !target) return false;
    FOR_LOOP(i, count) if (visited[i] == target) return true;
    return false;
}

/* Common authored metadata used by status-family procedures.  Keep fallback
 * policy in the owning ability; this only validates the primary BuffID field. */
cstring_t S_SpellBuffId(uint32_t code, uint32_t level) {
    cstring_t buff = G_AbilityLevel(code, level)->buffID;
    return buff && strlen(buff) >= 4 ? buff : NULL;
}

/* Warcraft stores separate normal/hero durations.  Some status families use
 * hero classification while crowd-control families use the broader resistant
 * predicate; keep those two policies explicit instead of passing a mode flag. */
float S_SpellHeroDuration(uint32_t code, uint32_t level, edict_t const *target) {
    return S_SpellDuration(code, level, target && G_UnitIsHero(target));
}

float S_SpellResistantDuration(uint32_t code, uint32_t level, edict_t const *target) {
    return S_SpellDuration(code, level, target && S_UnitIsResistant(target));
}

/* AbilityData BuffID fields can contain an ordered comma-separated list. Return
 * the requested token without allocating: Warcraft rawcodes are four bytes, so
 * callers may pass the returned pointer directly to FS_SLKKey/status helpers. */
cstring_t S_SpellBuffToken(cstring_t list, uint32_t index) {
    uint32_t current = 0;
    if (!list) return NULL;
    for (;;) {
        if (strlen(list) < 4) return NULL;
        if (current == index) return list;
        list = strchr(list, ',');
        if (!list) return NULL;
        list++;
        current++;
    }
}

/* Translate supported authored buff data into owned numeric contributions.
 * Existing ability procedures still own application and removal; the shared
 * resolver owns aggregation.  Use per-buff families to avoid accidentally
 * merging unrelated Warcraft stacking rules. */
/* Update exactly the contributor owned by authored AbilityData, never a
 * caller's custom contribution. Stable descriptor indices are not assumed. */
static bool status_authored_modifier(heroabilitystatus_t *slot,
                                     wc3_status_modifier_type_t type, float value) {
    wc3_status_modifier_t modifier = {
        .type = type, .family = slot->code,
        .policy = value < 0.0f ? WC3_STATUS_MOD_STRONGEST_NEGATIVE :
                                 WC3_STATUS_MOD_STRONGEST_POSITIVE,
        .value = value, .owner = WC3_STATUS_MOD_OWNER_AUTHORED
    };
    uint32_t index;
    /* An authored refresh must update its existing descriptor in place. */
    FOR_LOOP(i, MIN(slot->modifier_count, WC3_STATUS_MAX_MODIFIERS)) {
        wc3_status_modifier_t const *current = slot->modifiers + i;
        if (current->owner == WC3_STATUS_MOD_OWNER_AUTHORED && current->type == (uint32_t)type)
            return unit_status_setmodifier(slot, i, &modifier);
    }
    index = slot->modifier_count;
    /* Never evict an external descriptor to make room for an authored one. */
    return index < WC3_STATUS_MAX_MODIFIERS && unit_status_setmodifier(slot, index, &modifier);
}


#ifdef BZ_TESTS
TEST(wc3_spell, authored_modifier_refresh_keeps_external_contributors) {
    heroabilitystatus_t slot = { .level = 1, .code = MAKEFOURCC('B','i','n','f') };
    wc3_status_modifier_t custom = {
        .type = WC3_STATUS_MOD_ARMOR_FLAT, .policy = WC3_STATUS_MOD_ADD, .value = 7.0f
    };
    T_ASSERT(status_authored_modifier(&slot, WC3_STATUS_MOD_ARMOR_FLAT, 2.0f));
    T_ASSERT(unit_status_setmodifier(&slot, 1, &custom));
    T_ASSERT(status_authored_modifier(&slot, WC3_STATUS_MOD_ARMOR_FLAT, 5.0f));
    T_EQ(slot.modifier_count, 2);
    T_EQ(slot.modifiers[0].owner, WC3_STATUS_MOD_OWNER_AUTHORED);
    T_FEQ(slot.modifiers[0].value, 5.0f, 0.001f);
    T_EQ(slot.modifiers[1].owner, 0);
    T_FEQ(slot.modifiers[1].value, 7.0f, 0.001f);
}

TEST(wc3_spell, authored_modifier_full_slot_does_not_evict_custom_contributors) {
    heroabilitystatus_t slot = { .level = 1, .code = MAKEFOURCC('B','i','n','f') };
    FOR_LOOP(i, WC3_STATUS_MAX_MODIFIERS) {
        wc3_status_modifier_t custom = {
            .type = WC3_STATUS_MOD_ARMOR_FLAT, .policy = WC3_STATUS_MOD_ADD,
            .value = (float)i + 1.0f
        };
        T_ASSERT(unit_status_setmodifier(&slot, i, &custom));
    }
    T_ASSERT(!status_authored_modifier(&slot, WC3_STATUS_MOD_ARMOR_FLAT, 8.0f));
    T_EQ(slot.modifier_count, WC3_STATUS_MAX_MODIFIERS);
    FOR_LOOP(i, WC3_STATUS_MAX_MODIFIERS) {
        T_EQ(slot.modifiers[i].owner, 0);
        T_FEQ(slot.modifiers[i].value, (float)i + 1.0f, 0.001f);
    }
}
#endif

static void status_apply_authored_modifiers(heroabilitystatus_t *slot, uint32_t ability) {
    uint32_t rank;
    if (!slot || !slot->level) return;
    rank = slot->rank ? slot->rank : slot->level;
    /* Only stock families whose modifiers are owned by this resolver may be rebuilt. */
    switch (slot->code) {
    case MAKEFOURCC('B','c','r','i'): case MAKEFOURCC('B','b','l','o'):
    case MAKEFOURCC('B','b','s','k'):
    case MAKEFOURCC('B','s','l','o'): case MAKEFOURCC('B','c','s','d'):
    case MAKEFOURCC('B','f','r','o'): case MAKEFOURCC('B','f','z','y'):
    case MAKEFOURCC('B','U','h','f'):
    case MAKEFOURCC('B','u','h','f'): case MAKEFOURCC('B','s','p','o'):
    case MAKEFOURCC('B','i','n','f'): case MAKEFOURCC('B','f','a','e'):
    case MAKEFOURCC('B','U','f','a'):
    case MAKEFOURCC('B','H','t','c'): case MAKEFOURCC('B','N','d','h'): case MAKEFOURCC('B','N','h','t'):
        break;
    default: return;
    }
    /* Unknown/custom statuses own their descriptors: never erase them. */
    switch (slot->code) {
    case MAKEFOURCC('B','c','r','i'):
        if (!ability) ability = MAKEFOURCC('A','c','r','i');
        status_authored_modifier(slot, WC3_STATUS_MOD_MOVE_SPEED_PERCENT, -S_SpellData(ability, rank, 1));
        status_authored_modifier(slot, WC3_STATUS_MOD_ATTACK_SPEED_PERCENT, -S_SpellData(ability, rank, 2));
        status_authored_modifier(slot, WC3_STATUS_MOD_ATTACK_DAMAGE_PERCENT, -S_SpellData(ability, rank, 3));
        break;
    case MAKEFOURCC('B','b','l','o'):
        if (!ability) ability = MAKEFOURCC('A','b','l','o');
        status_authored_modifier(slot, WC3_STATUS_MOD_ATTACK_SPEED_PERCENT, S_SpellData(ability, rank, 1));
        status_authored_modifier(slot, WC3_STATUS_MOD_MOVE_SPEED_PERCENT, S_SpellData(ability, rank, 2));
        break;
    case MAKEFOURCC('B','b','s','k'):
        ability = MAKEFOURCC('A','b','s','k');
        status_authored_modifier(slot, WC3_STATUS_MOD_ATTACK_SPEED_PERCENT, S_SpellData(ability, rank, 2));
        status_authored_modifier(slot, WC3_STATUS_MOD_DAMAGE_TAKEN_PERCENT, S_SpellData(ability, rank, 3));
        break;
    case MAKEFOURCC('B','s','l','o'):
        /* A shared stock BuffID does not replace the casting alias's data. */
        if (!ability) ability = MAKEFOURCC('A','s','l','o');
        status_authored_modifier(slot, WC3_STATUS_MOD_ATTACK_SPEED_PERCENT, -S_SpellData(ability, rank, 2));
        break;
    case MAKEFOURCC('B','f','z','y'):
        ability = MAKEFOURCC('A','f','z','y');
        status_authored_modifier(slot, WC3_STATUS_MOD_ATTACK_SPEED_PERCENT, S_SpellData(ability, rank, 1));
        status_authored_modifier(slot, WC3_STATUS_MOD_ARMOR_FLAT, -S_SpellData(ability, rank, 2));
        break;
    case MAKEFOURCC('B','U','h','f'):
    case MAKEFOURCC('B','u','h','f'):
        ability = MAKEFOURCC('A','u','h','f');
        status_authored_modifier(slot, WC3_STATUS_MOD_ATTACK_SPEED_PERCENT, S_SpellData(ability, rank, 1));
        break;
    case MAKEFOURCC('B','s','p','o'):
        ability = MAKEFOURCC('A','s','p','o');
        status_authored_modifier(slot, WC3_STATUS_MOD_MOVE_SPEED_PERCENT, -S_SpellData(ability, rank, 2));
        status_authored_modifier(slot, WC3_STATUS_MOD_ATTACK_SPEED_PERCENT, -S_SpellData(ability, rank, 3));
        break;
    case MAKEFOURCC('B','c','s','d'):
        /* Sea Witch Frost Arrows carry rank through their shared stacking
         * debuff; retain the concrete ability alias for authored DataB/C. */
        if (!ability) ability = MAKEFOURCC('A','H','c','a');
        status_authored_modifier(slot, WC3_STATUS_MOD_MOVE_SPEED_PERCENT, -S_SpellData(ability, rank, 2));
        status_authored_modifier(slot, WC3_STATUS_MOD_ATTACK_SPEED_PERCENT, -S_SpellData(ability, rank, 3));
        break;
    case MAKEFOURCC('B','f','r','o'):
        /* TFT Frost Attack (Afra) uses Bfro and authors duration, but the
         * classic slow magnitudes are intrinsic to this buff. */
        status_authored_modifier(slot, WC3_STATUS_MOD_MOVE_SPEED_PERCENT, -0.5f);
        status_authored_modifier(slot, WC3_STATUS_MOD_ATTACK_SPEED_PERCENT, -0.25f);
        break;
    case MAKEFOURCC('B','U','f','a'):
        /* Frost Armor stores its armor bonus in DataB; DataA is not armor. */
        if (!ability) ability = MAKEFOURCC('A','U','f','a');
        status_authored_modifier(slot, WC3_STATUS_MOD_ARMOR_FLAT,
                                 S_SpellData(ability, rank, 2));
        break;
    case MAKEFOURCC('B','i','n','f'):
        if (!ability) ability = MAKEFOURCC('A','i','n','f');
        status_authored_modifier(slot, WC3_STATUS_MOD_ARMOR_FLAT, S_SpellData(ability, rank, 2));
        status_authored_modifier(slot, WC3_STATUS_MOD_ATTACK_DAMAGE_PERCENT, S_SpellData(ability, rank, 1));
        break;
    case MAKEFOURCC('B','f','a','e'):
        ability = MAKEFOURCC('A','f','a','e');
        status_authored_modifier(slot, WC3_STATUS_MOD_ARMOR_FLAT, -S_SpellData(ability, rank, 1));
        break;
    case MAKEFOURCC('B','H','t','c'):
        if (!ability) ability = MAKEFOURCC('A','H','t','c');
        status_authored_modifier(slot, WC3_STATUS_MOD_MOVE_SPEED_PERCENT, -S_SpellData(ability, rank, 3));
        status_authored_modifier(slot, WC3_STATUS_MOD_ATTACK_SPEED_PERCENT, -S_SpellData(ability, rank, 4));
        break;
    case MAKEFOURCC('B','N','d','h'):
        if (!ability) ability = MAKEFOURCC('A','N','d','h');
        status_authored_modifier(slot, WC3_STATUS_MOD_MOVE_SPEED_PERCENT,
                                -S_SpellData(ability, rank, 3)); /* DataC */
        break;
    case MAKEFOURCC('B','N','h','t'):
        if (!ability) ability = MAKEFOURCC('A','N','h','t');
        status_authored_modifier(slot, WC3_STATUS_MOD_ATTACK_DAMAGE_PERCENT, -S_SpellData(ability, rank, 1));
        break;
    default: return;
    }
}

/* Lowest common timed-status lifecycle: apply/replace the authored status and
 * return its authoritative slot.  Presentation and payload remain caller-owned. */
heroabilitystatus_t *S_SpellApplyTimedStatus(edict_t *target, cstring_t buff, uint32_t level, float duration) {
    if (!target || !buff || strlen(buff) < 4) return NULL;
    status_application_t app = { .buff = buff, .level = level, .duration = duration };
    heroabilitystatus_t *status = unit_applystatus(target, &app);
    if (status) status_apply_authored_modifiers(status, status->source_ability);
    return status;
}

/* Freezing Breath uses a timed status as the saveable owner of a building's
 * disabled interval. Its gameplay lock is consumed by building production
 * and attack paths rather than by a generic stun state. */
bool S_UnitIsFreezingBreathFrozen(edict_t const *unit) {
    heroabilitystatus_t const *status = unit_findstatus((edict_t *)unit, MAKEFOURCC('B','f','r','z'));
    return status && (!status->timestamp || (int32_t)(status->timestamp - G_Time()) > 0);
}

/* BTLF is lifecycle ownership rather than an ordinary dispellable buff.
 * Callers still decide whether a zero duration means permanent lifecycle state
 * or whether no timed-life marker should be created at all. */
heroabilitystatus_t *S_SpellApplyTimedLife(edict_t *unit, uint32_t level, float duration) {
    return S_SpellApplyTimedStatus(unit, "BTLF", level, duration);
}

/* Apply a timed status with its originating ability identity so authored
 * modifiers use the correct row even when several abilities share a buff. */
heroabilitystatus_t *S_SpellApplyTimedStatusFromAbility(edict_t *target, uint32_t code, uint32_t level,
                                                        cstring_t buff, float duration) {
    status_application_t app = { .buff = buff, .level = level, .duration = duration,
                                 .source_ability = code, .rank = level };
    heroabilitystatus_t *status;
    if (!target || !buff || strlen(buff) < 4) return NULL;
    status = unit_applystatus(target, &app);
    if (!status) return NULL;
    status_apply_authored_modifiers(status, code);
    return status;
}

/* Ordinary target buffs add the standard authored TargetArt on top of the
 * shared timed-status lifecycle. */
heroabilitystatus_t *S_SpellApplyTimedTargetStatus(edict_t *target, uint32_t code, uint32_t level, cstring_t buff, float duration) {
    heroabilitystatus_t *status = S_SpellApplyTimedStatusFromAbility(target, code, level, buff, duration);
    uint32_t buff_code;
    edict_t *effect;
    if (!status) return NULL;
    effect = G_SpawnAbilityEffectTarget(code, WC3_EFFECT_TARGET, 0, target, NULL, true);
    /* Some spells author the persistent target model on the applied buff
     * instead of the ability (Sleep's AUsl/BUsl is one example). In that case
     * let the status own the effect so refresh, dispel, and expiry clean it up. */
    if (!effect && buff && strlen(buff) >= 4) {
        memcpy(&buff_code, buff, sizeof(buff_code));
        if (G_AbilityEffectArt(buff_code, WC3_EFFECT_TARGET, 0))
            unit_status_enabletargetart(target, status, NULL);
    }
    return status;
}

/* Warcraft's standard stun lifecycle is always Bstu at status level one.
 * Callers own hit validation and the exact authored/resistant duration. */
void S_SpellApplyStun(edict_t *target, float duration) {
    heroabilitystatus_t *status = S_SpellApplyTimedStatus(target, "Bstu", 1, duration);
    if (status) unit_status_enabletargetart(target, status, "overhead");
}

/* Fixed-damage point/radius family used by simple area nukes. More selective
 * ground/cone/target-mask/status variants keep their own enumeration policy. */
void S_SpellDamageEnemiesInRadius(edict_t *caster, vec2_t const *center, float radius, uint32_t damage) {
    if (!caster || !center) return;
    FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellIsEnemy(caster, target) &&
                  Vector2_distance(&target->s.origin2, center) <= radius)
        S_SpellDamage(target, caster, damage);
}

/* Simple persistent on/off status family. Specialized toggles such as Defend
 * keep their own wrapper so animation and expiry policy remain ability-owned. */
void S_ToggleUnitAbilityStatus(edict_t *unit, uint32_t code, uint32_t level) {
    if (!unit || !code) return;
    FOR_LOOP(i, unit->unit_state ? MAX_UNIT_STATUSES : 0) {
        heroabilitystatus_t *status = unit->unit_state->statuses + i;
        if (status->level && status->code == code) {
            unit_removestatus(unit, status, STATUS_REMOVE_SCRIPT);
            return;
        }
    }
    unit_addstatus(unit, GetClassName(code), level);
}

bool S_IssueUnitTargetSpell(edict_t *caster, uint32_t code, edict_t *unit) {
    uint32_t level;
    float range;
    ability_t const *spell;
    spellTarget_t target = { .type = SPELL_TARGET_UNIT, .entity = unit };

    if (!caster || !unit || !code || !G_UnitAbilityLevel(caster, code) || S_UnitPolymorphed(caster)) return false;
    spell = S_SpellAbilityForCode(code);
    abilityitem_t item = { .code = code, .ability = spell };
    if (!spell || !S_AbilityHasCommand(spell) || (spell->flags & AB_TOGGLE)) return false;
    level = S_SpellLevel(caster, code);
    if (spell_is_tree_interaction(spell)) {
        spellPointValidateParams_t val = MAKE(spellPointValidateParams_t,
            .caster = caster, .code = code, .level = level, .point = &unit->s.origin2, .range = 0.0f);
        float const cast_range = spell_tree_target_range(code, level, caster);
        bool const valid = spell->proc == CAbilityEatTree ?
            spell_validate(NULL, caster, code, level, unit, 0.0f, NULL) : spell_validate_point(&val);
        if (!valid || !spell_message(caster, A_VALIDATE, &item, &target)) return false;
        if (Vector2_distance(&caster->s.origin2, &unit->s.origin2) > cast_range)
            return spell_begin_target_approach(caster, code, unit, NULL, NULL, 0);
        return spell_execute_point_target(NULL, caster, code, level, spell, &unit->s.origin2,
                                          unit, NULL, 0, NULL);
    }
    if (spell->target_type != SPELL_TARGET_UNIT && spell->target_type != SPELL_TARGET_UNIT_OR_POINT)
        return false;
    range = S_SpellRange(code, level);
    if (!spell_validate(NULL, caster, code, level, unit, 0.0f, NULL) ||
        !S_SpellAllowsTarget(code, caster, unit)) return false;
    if (!spell_message(caster, A_VALIDATE, &item, &target)) return false;
    if (!S_SpellTargetInRange(caster, unit, range))
        return spell_begin_target_approach(caster, code, unit, NULL, NULL, 0);

    spellUnitTargetParams_t params = {
        .caster = caster, .code = code, .level = level, .spell = spell, .target = unit
    };
    spell_execute_unit_target(&params, NULL);
    return true;
}

bool S_IssuePointTargetSpell(edict_t *caster, uint32_t code, vec2_t const *point) {
    uint32_t level;
    float range;
    ability_t const *spell;
    abilityitem_t item;
    spellTarget_t target;
    spellPointValidateParams_t val;

    if (!caster || !point || !code || !G_UnitAbilityLevel(caster, code) || S_UnitPolymorphed(caster))
        return false;
    spell = S_SpellAbilityForCode(code);
    item = MAKE(abilityitem_t, .code = code, .ability = spell);
    if (!spell || (spell->target_type != SPELL_TARGET_POINT &&
                   spell->target_type != SPELL_TARGET_UNIT_OR_POINT) ||
        !S_AbilityHasCommand(spell) || (spell->flags & AB_TOGGLE)) return false;
    level = S_SpellLevel(caster, code);
    range = S_SpellRange(code, level);
    val = MAKE(spellPointValidateParams_t, .caster = caster, .code = code,
               .level = level, .point = point, .range = 0.0f);
    if (!spell_validate_point(&val)) return false;
    target = MAKE(spellTarget_t, .type = SPELL_TARGET_POINT, .point = *point);
    if (!spell_message(caster, A_VALIDATE, &item, &target)) return false;

    if (range > 0.0f && Vector2_distance(&caster->s.origin2, point) > range)
        return spell_begin_target_approach(caster, code, NULL, point, NULL, 0);

    spell_execute_point_target(NULL, caster, code, level, spell, point, NULL, NULL, 0, NULL);
    return true;
}

/* Shared command entry point for all spell abilities.  Sets up the appropriate
 * target-selection UI based on ability_t.target_type, or executes
 * immediately for no-target spells. */
void spell_cmd(edict_t *clent) {
    edict_t *caster = G_GetMainSelectedUnit(clent->client);
    uint32_t code = S_SpellCurrentCode(clent, 0);
    ability_t const *spell = S_SpellAbilityForCode(code);
    abilityitem_t item = { .code = code, .ability = spell };
    abilityCall_t toggle_call = MAKE(abilityCall_t, .item = &item);

    if (!spell) {
        fprintf(stderr, "spell_cmd: no executable spell ability for code '%.4s'\n", (cstring_t)&code);
        return;
    }
    if (!caster) {
        return;
    }

    if (!G_UnitAbilityResearchAvailable(caster, code)) {
        G_ShowCommandErrorText(clent, "Requires training.");
        return;
    }
    if (!G_IsUnitAbilityAvailable(caster, code)) {
        G_ShowCommandErrorText(clent, "This ability is unavailable.");
        return;
    }

    /* Anti-Magic Potion is an inventory self-use item. Its shared AIxs spell
     * implementation is unit-targeted for the Aami ability variant, but a
     * carried potion applies directly to its hero on the inventory click. */
    if (code == MAKEFOURCC('A','I','x','s') && clent->client->menu.ability_item &&
        spell_item_source_valid(caster, clent->client->menu.ability_item,
                                clent->client->menu.ability_item_spawn_time)) {
        uint32_t const level = S_SpellLevel(caster, code);
        spellTarget_t self = MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = caster);
        spellUnitTargetParams_t params;
        if (!spell_validate(clent, caster, code, level, caster, 0.0f, clent->client->menu.ability_item) ||
            !S_SpellAllowsTarget(code, caster, caster) ||
            !spell_message(caster, A_VALIDATE, &item, &self)) return;
        params = MAKE(spellUnitTargetParams_t, .caster = caster, .code = code, .level = level,
                      .spell = spell, .target = caster,
                      .source_item = clent->client->menu.ability_item,
                      .source_item_spawn_time = clent->client->menu.ability_item_spawn_time);
        spell_execute_unit_target(&params, NULL);
        return;
    }

    if (spell->proc == CAbilityCoupleHippogryph || spell->proc == CAbilityCoupleArcher) {
        if (S_CastNearestCouplePartner(caster, code)) spell_order_accepted(caster, spell);
        return;
    }

    /* Toggle abilities bypass the normal pipeline. */
    if ((spell->flags & AB_TOGGLE) && code != MAKEFOURCC('A','f','a','k') &&
        spell->proc != CAbilityFlamingArrows &&
        spell->proc != CAbilityColdArrows && spell->proc != CAbilityBlackArrow &&
        (spell->proc != CAbilityManaFlare || S_AbilityMessage(caster, A_TOGGLE_ON, &toggle_call))) {
        spellTarget_t target = { .type = SPELL_TARGET_NONE };
        spell_message(caster, A_EXECUTE, &item, &target);
        Get_Commands_f(clent);
        return;
    }


    switch (spell->target_type) {
    case SPELL_TARGET_NONE:
        spell_no_target_execute(clent);
        break;
    case SPELL_TARGET_UNIT:
        UI_AddCancelButton(clent);
        clent->client->menu.on_entity_selected = spell_unit_target_selected;
        break;
    case SPELL_TARGET_POINT: {
        /* Resolve the Tiny cursor before opening target mode: the Cancel
         * button used to be added first, so an unresolvable cursor (an AIbg
         * row without a UnitID) left a dead Cancel button with no
         * on_location_selected handler behind it. */
        if (spell_is_tiny_structure(spell) && !spell_tiny_cursor_show(clent, caster, code)) {
            fprintf(stderr, "spell_cmd: Tiny Structure '%.4s' has no buildable UnitID\n", (cstring_t)&code);
            return;
        }
        UI_AddCancelButton(clent);
        clent->client->menu.ability_code = code;
        abilityCall_t target_mode = MAKE(abilityCall_t, .item = &item);
        spell->proc(caster, A_SPELL_TARGET_MODE, &target_mode);
        bool const entity_target = target_mode.spell_entity_target_mode;
        if (entity_target) {
            clent->client->menu.on_location_selected = NULL;
            clent->client->menu.on_entity_selected = spell_ability_entity_target_selected;
        } else if (!spell_is_tiny_structure(spell)) {
            uint32_t const display_code = S_TinkerVariant(caster, code);
            uint32_t const display_level = display_code == code ? S_SpellLevel(caster, code) : 1;
            float area = S_SpellNumber(display_code, ABILITY_NUMBER_AREA, display_level);
            S_SpellCursorSplat(clent, area);
        }
        if (!entity_target)
            clent->client->menu.on_location_selected = spell_point_target_selected;
        break;
    }
    case SPELL_TARGET_UNIT_OR_POINT:
        spell_no_target_execute(clent); /* TODO: add unit-or-point fallback via smart-click */
        break;
    }
}
