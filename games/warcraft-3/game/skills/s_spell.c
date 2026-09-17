#include "s_skills.h"

#include <ctype.h>
#include <math.h>

#define DEFAULT_SPELL_AREA_CURSOR "ReplaceableTextures\\Selection\\SpellAreaOfEffect.blp"

typedef struct {
    LPEDICT caster;
    DWORD code, level;
    ability_t const *spell;
    LPEDICT target;
} spellUnitTargetParams_t;

typedef struct {
    LPEDICT clent, caster;
    DWORD code, level;
    LPCVECTOR2 point;
    FLOAT range;
} spellPointValidateParams_t;

/* ---- Unified Spell Pipeline ----

 * All hero/unit spells route through a single cmd entry point (spell_cmd) that
 * reads the ability code, validates mana/cooldown, configures targeting, and
 * dispatches an A_EXECUTE message once a valid target is
 * acquired.  channeled spells set ent->channel state; spell_run_frame()
 * enforces movement-cancel for them.
 *
 * Design mirrors:
 *   - WarSmash: CAbilitySpellBase with target-type dispatch
 *   - WoW: data-driven spell table + cast state machine (Wow_RunSpellCast)
 *   - Quake2: flat flags and callbacks around a shared processor */

static LPCSTR S_SpellThemeString(LPCSTR key, LPCSTR def) {
    LPCSTR value = NULL;

    if (key && !strstr(key, "\\") && game.config.theme.source) {
        value = Stb_IniCacheFind(&game.config.theme, "Default", key);
    }
    return value ? value : def;
}

/* Build the borrowed typed payload used by the synchronous spell messages. */
static intptr_t spell_message(LPEDICT ent, abilityMsg_t msg, abilityitem_t const *item, spellTarget_t const *target) {
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
    (void)ent;
    if (!call || !call->item || !call->item->ability) return false;
    switch (msg) {
    case A_COMMAND:
        if (!call->client) return false;
        spell_cmd(call->client);
        return true;
    case A_VALIDATE: return true;
    case A_MOVE_LEAVE:
        if (ent && ent->channel.code == call->item->code) S_SpellCancelChannel(ent);
        return true;
    default:
        return false;
    }
}

/* Modal spells share autocast selection while concrete children own acquisition and effects. */
BZ_ABILITY_PROC(CAbilityModalSpell) {
    DWORD code = call && call->item ? call->item->code : 0;
    switch (msg) {
    case A_AUTOCAST_ON: return ent && ent->autocast_code == code;
    case A_AUTOCAST_SET: return true;
    default: return CAbilitySimpleSpell(ent, msg, call);
    }
}

void S_SpellCodeString(DWORD code, LPSTR out) {
    memcpy(out, &code, 4);
    out[4] = '\0';
}

DWORD S_SpellCurrentCode(LPEDICT clent, DWORD fallback) {
    DWORD code = clent && clent->client ? clent->client->menu.ability_code : 0;
    return code ? code : fallback;
}

ability_t const *S_SpellAbilityForCode(DWORD code) {
    ability_t const *ability;

    if (!code) return NULL;

    /* A rawcode stored in a DWORD is four bytes, not a C string. The old
     * spell path cast &code to LPCSTR and handed it to strcmp(), so lookup
     * depended on the unrelated byte immediately after the DWORD being zero.
     * Convert through GetClassName(), which supplies the required terminator,
     * and use the normal alias-aware command resolver so custom abilities
     * inherit their registered base handler as well. */
    ability = FindAbilityForCommand(GetClassName(code));
    return ability && (ability->flags & AB_SPELL) && ability->proc ? ability : NULL;
}

DWORD S_SpellLevel(LPEDICT caster, DWORD code) {
    if (!caster) {
        return 1;
    }
    FOR_LOOP(i, MAX_HERO_ABILITIES) {
        heroability_t const *ha = caster->heroabilities + i;
        if (ha->level && ha->code == code) {
            return ha->level;
        }
    }
    return 1;
}

FLOAT S_SpellNumber(DWORD code, abilityNumber_t field, DWORD level) {
    abilityLevel_t const *row = G_AbilityLevel(code, level);
    level = MAX(1, MIN(level, 4));
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

LPCSTR S_SpellString(DWORD code, LPCSTR field, DWORD level) {
    char code_string[5];
    LPCSTR value;

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
FLOAT S_SpellData(DWORD code, DWORD level, DWORD index) {
    char classname[5] = {0};
    memcpy(classname, &code, 4);
    return AB_Data(classname, level, index);
}

DWORD S_SpellDataId(DWORD code, DWORD level, DWORD index) {
    char classname[5] = {0};
    memcpy(classname, &code, 4);
    return AB_DataId(classname, level, index);
}

DWORD S_SpellUnitId(DWORD code, DWORD level) {
    return G_AbilityLevel(code, level)->unitID;
}

FLOAT S_SpellRange(DWORD code, DWORD level) {
    return S_SpellNumber(code, ABILITY_NUMBER_RANGE, level);
}

FLOAT S_SpellDuration(DWORD code, DWORD level, BOOL hero) {
    return S_SpellNumber(code, hero ? ABILITY_NUMBER_HERO_DURATION : ABILITY_NUMBER_DURATION, level);
}

static void S_SpellInvalidateCooldownUI(LPEDICT caster) {
    LPGAMECLIENT client;
    if (!caster) return;
    client = G_GetPlayerClientByNumber(caster->s.player);
    if (client && client->ps.number == caster->s.player) G_InvalidateCommands(client);
}

static DWORD S_SpellCooldownCode(DWORD code) {
    return code ? G_AbilityCode(code) : 0;
}

static abilityCooldown_t *S_SpellFindCooldown(LPEDICT caster, DWORD code) {
    DWORD const cooldown_code = S_SpellCooldownCode(code);

    if (!caster || !cooldown_code) return NULL;
    FOR_LOOP(i, MAX_UNIT_COOLDOWNS) {
        abilityCooldown_t *cooldown = caster->abilitycooldowns + i;
        if (cooldown->code == cooldown_code) return cooldown;
    }
    return NULL;
}

static abilityCooldown_t *S_SpellAllocCooldown(LPEDICT caster, DWORD code) {
    DWORD const cooldown_code = S_SpellCooldownCode(code);
    DWORD const now = G_Time();
    abilityCooldown_t *available = NULL;

    if (!caster || !cooldown_code) return NULL;
    FOR_LOOP(i, MAX_UNIT_COOLDOWNS) {
        abilityCooldown_t *cooldown = caster->abilitycooldowns + i;
        if (cooldown->code == cooldown_code) return cooldown;
        if (!available && (!cooldown->code || (LONG)(cooldown->end_time - now) <= 0)) available = cooldown;
    }
    if (available) {
        memset(available, 0, sizeof(*available));
        available->code = cooldown_code;
    }
    return available;
}

BOOL S_SpellCooldownReady(LPEDICT caster, DWORD code) {
    abilityCooldown_t const *cooldown = S_SpellFindCooldown(caster, code);
    return !cooldown || (LONG)(cooldown->end_time - G_Time()) <= 0;
}

FLOAT S_SpellCooldownRemaining(LPEDICT caster, DWORD code) {
    abilityCooldown_t const *cooldown = S_SpellFindCooldown(caster, code);
    DWORD const now = G_Time();
    if (!cooldown || (LONG)(cooldown->end_time - now) <= 0) return 0.0f;
    return (FLOAT)(DWORD)(cooldown->end_time - now) / 1000.0f;
}

FLOAT S_SpellCooldownLength(LPEDICT caster, DWORD code) {
    abilityCooldown_t const *cooldown = S_SpellFindCooldown(caster, code);
    if (!cooldown || cooldown->end_time == cooldown->start_time) return 0.0f;
    return (FLOAT)(DWORD)(cooldown->end_time - cooldown->start_time) / 1000.0f;
}

BOOL S_SpellCooldownWindow(LPEDICT caster, DWORD code, abilityCooldownWindow_t *window) {
    abilityCooldown_t const *cooldown = S_SpellFindCooldown(caster, code);
    if (!cooldown || !window || (LONG)(cooldown->end_time - G_Time()) <= 0) return false;
    window->start_time = cooldown->start_time;
    window->end_time = cooldown->end_time;
    return true;
}

/* Fraction of an ability's authored cooldown still remaining. The total comes
 * from the cooldown record captured at cast time, so learning another level
 * while a cooldown is active cannot make the command-card sweep jump. */
FLOAT S_SpellCooldownFraction(LPEDICT caster, DWORD code, DWORD level) {
    FLOAT const remaining = S_SpellCooldownRemaining(caster, code);
    FLOAT const total = S_SpellCooldownLength(caster, code);
    (void)level;
    if (remaining <= 0.0f || total <= 0.0f) return 0.0f;
    return MIN(1.0f, remaining / total);
}

void S_SpellStartCooldownDuration(LPEDICT caster, DWORD code, FLOAT duration) {
    abilityCooldown_t *cooldown;
    DWORD duration_ms;

    if (!caster || !code) return;
    if (duration <= 0.0f) {
        S_SpellEndCooldown(caster, code);
        return;
    }
    cooldown = S_SpellAllocCooldown(caster, code);
    if (!cooldown) return;
    duration_ms = (DWORD)ceilf(duration * 1000.0f);
    cooldown->code = S_SpellCooldownCode(code);
    cooldown->start_time = G_Time();
    cooldown->end_time = cooldown->start_time + MAX(duration_ms, 1u);
    S_SpellInvalidateCooldownUI(caster);
}

void S_SpellStartCooldown(LPEDICT caster, DWORD code, DWORD level) {
    S_SpellStartCooldownDuration(caster, code,
        S_SpellNumber(code, ABILITY_NUMBER_COOLDOWN, level));
}

void S_SpellEndCooldown(LPEDICT caster, DWORD code) {
    abilityCooldown_t *cooldown = S_SpellFindCooldown(caster, code);
    if (cooldown) {
        memset(cooldown, 0, sizeof(*cooldown));
        S_SpellInvalidateCooldownUI(caster);
    }
}

void S_SpellResetCooldowns(LPEDICT caster) {
    if (!caster) return;
    memset(caster->abilitycooldowns, 0, sizeof(caster->abilitycooldowns));
    S_SpellInvalidateCooldownUI(caster);
}

BOOL S_SpellSpendMana(LPEDICT caster, DWORD code, DWORD level) {
    FLOAT cost;

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

BOOL S_SpellCanPay(LPEDICT caster, DWORD code, DWORD level) {
    FLOAT cost;

    if (!caster) {
        return false;
    }
    cost = S_SpellNumber(code, ABILITY_NUMBER_COST, level);
    return cost <= 0 || caster->mana.value >= cost;
}

BOOL S_SpellTargetInRange(LPEDICT caster, LPEDICT target, FLOAT range) {
    if (!caster || !target) {
        return false;
    }
    return range <= 0 || Vector2_distance(&caster->s.origin2, &target->s.origin2) <= range;
}

BOOL S_SpellIsAliveTarget(LPEDICT target) {
    return target && target->inuse && (target->svflags & SVF_MONSTER) && !M_IsDead(target);
}

BOOL S_SpellIsEnemy(LPEDICT caster, LPEDICT target) {
    DWORD owner;

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

BOOL S_SpellIsFriend(LPEDICT caster, LPEDICT target) {
    DWORD owner;

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

BOOL S_SpellAllowsTarget(DWORD code, LPEDICT caster, LPEDICT target) {
    LPCSTR targets;

    if (!S_SpellIsAliveTarget(target) || S_UnitIsCycloned(target)) {
        return false;
    }
    if (S_UnitSpellImmune(target)) return false;
    targets = G_AbilityLevel(code, 1)->targs;
    if (!targets) {
        return true;
    }
    if ((strstr(targets, "air") || strstr(targets, "ground")) &&
        !(strstr(targets, "air") && target->targtype == TARG_AIR) &&
        !(strstr(targets, "ground") && target->targtype == TARG_GROUND)) {
        return false;
    }
    if (strstr(targets, "friend") && S_SpellIsFriend(caster, target)) {
        return true;
    }
    if (strstr(targets, "enemy") && S_SpellIsEnemy(caster, target)) {
        return true;
    }
    if (strstr(targets, "neutral") && target->s.player < MAX_PLAYERS && level.mapinfo &&
        level.mapinfo->players[target->s.player].playerType == kPlayerTypeNeutral) {
        return true;
    }
    return !strstr(targets, "friend") && !strstr(targets, "enemy") && !strstr(targets, "neutral");
}

void S_SpellHeal(LPEDICT target, FLOAT amount) {
    if (!target || amount <= 0) {
        return;
    }
    G_AddHealth(target, amount);
}

void S_SpellCursorSplat(LPEDICT clent, FLOAT radius) {
    LONG image = 0;

    if (!clent || !clent->client) {
        return;
    }
    if (radius > 0.0f) {
        image = gi.ImageIndex(S_SpellThemeString("PlacementCursor", DEFAULT_SPELL_AREA_CURSOR));
    } else {
        radius = 0.0f;
    }
    gi.Write(PF_BYTE, &(LONG){ svc_cursor_splat });
    gi.Write(PF_SHORT, &image);
    gi.Write(PF_FLOAT, &radius);
    gi.unicast(clent);
}

BOOL S_SpellIsChanneling(LPEDICT caster) {
    return caster && caster->channel.code != 0;
}

void S_SpellCancelChannel(LPEDICT caster) {
    if (!caster || !caster->channel.code) {
        return;
    }
    caster->channel.code = 0;
}

/* A cast serial and owner incarnation prevent a retired thinker from following a recast or reused edict. */
LPEDICT S_SpellChannelThinker(LPEDICT caster, DWORD code) {
    LPEDICT ent = G_Spawn();
    ent->owner = caster; ent->class_id = code;
    ent->channel.serial = caster->channel.serial;
    ent->channel.owner_spawn_time = caster->spawn_time;
    return ent;
}

/* Each effect rechecks the caster before ticking, independently of edict iteration order. */
BOOL S_SpellChannelActive(LPEDICT ent) {
    LPEDICT caster = ent ? ent->owner : NULL;
    if (!caster || !caster->inuse || caster->spawn_time != ent->channel.owner_spawn_time) return false;
    spell_run_frame(caster);
    return !M_IsDead(caster) && caster->channel.code == ent->class_id &&
        caster->channel.serial == ent->channel.serial;
}

/* Ending an old thinker must never cancel a replacement order or a newer cast of the same spell. */
void S_SpellEndChannel(LPEDICT ent) {
    LPEDICT caster = ent->owner;
    if (caster && caster->inuse && caster->spawn_time == ent->channel.owner_spawn_time &&
        caster->channel.code == ent->class_id && caster->channel.serial == ent->channel.serial)
        S_SpellCancelChannel(caster);
    G_FreeEdict(ent);
}

/* ---- Unified Spell Pipeline ---- */

/* Per-frame channel enforcement: if the caster has moved from cast_origin,
 * cancel the channel.  Called from G_RunEntity. */
void spell_run_frame(LPEDICT ent) {
    if (!ent->channel.code)
        return;

    /* Stun or death interrupts channel. */
    if (ent->stunned || M_IsDead(ent)) {
        S_SpellCancelChannel(ent);
        return;
    }

    /* Movement cancel: caster moved from the position where channel began. */
    if (fabsf(ent->s.origin.x - ent->channel.origin.x) > 0.5f ||
        fabsf(ent->s.origin.y - ent->channel.origin.y) > 0.5f) {
        S_SpellCancelChannel(ent);
        return;
    }
}

/* Shared validation for spell spells: mana, cooldown, and optional range check. */
static BOOL spell_validate(LPEDICT clent, LPEDICT caster, DWORD code, DWORD level, LPEDICT target, FLOAT range) {
    if (!S_SpellIsAliveTarget(caster) || caster->stunned || S_UnitPolymorphed(caster) || S_UnitIsCycloned(caster)) return false;
    if (S_UnitHasStatus(caster, MAKEFOURCC('B','N','s','i'))) {
        G_ShowCommandErrorText(clent, "Silenced.");
        return false;
    }
    if (!S_SpellCooldownReady(caster, code)) {
        G_ShowCommandErrorText(clent, "Spell is not ready yet.");
        return false;
    }
    if (!S_SpellCanPay(caster, code, level)) {
        G_ShowCommandErrorText(clent, "Not enough mana.");
        return false;
    }
    if (range > 0 && target && !S_SpellTargetInRange(caster, target, range))
        return false;
    return true;
}

/* Shared validation for point-target spells. */
static BOOL spell_validate_point(spellPointValidateParams_t const *params) {
    if (!params || !params->caster || !params->point)
        return false;
    if (!S_SpellIsAliveTarget(params->caster) || params->caster->stunned || S_UnitPolymorphed(params->caster) ||
        S_UnitIsCycloned(params->caster)) return false;
    if (S_UnitHasStatus(params->caster, MAKEFOURCC('B','N','s','i'))) {
        G_ShowCommandErrorText(params->clent, "Silenced.");
        return false;
    }
    if (!S_SpellCooldownReady(params->caster, params->code)) {
        G_ShowCommandErrorText(params->clent, "Spell is not ready yet.");
        return false;
    }
    if (!S_SpellCanPay(params->caster, params->code, params->level)) {
        G_ShowCommandErrorText(params->clent, "Not enough mana.");
        return false;
    }
    if (params->range > 0 && Vector2_distance(&params->caster->s.origin2, params->point) > params->range)
        return false;
    return true;
}

/* Start channel: lock caster in place and record the origin for movement-cancel. */
static void spell_begin_channel(LPEDICT caster, DWORD code) {
    if (caster->stand) caster->stand(caster);
    caster->channel.serial++;
    caster->channel.code = code;
    caster->channel.origin = caster->s.origin2;
}

/* Pre-execute common work: spend mana, start cooldown. */
static void spell_commit(LPEDICT caster, DWORD code, DWORD level) {
    S_SpellCancelChannel(caster);
    S_HumanBreakInvisibility(caster);
    S_SpellSpendMana(caster, code, level);
    S_SpellStartCooldown(caster, code, level);
}

/* Warcraft exposes spell response data only while dispatching the spell event.
 * Publish SPELL_EFFECT at the irreversible cast point: resources have been
 * committed, but the gameplay callback has not run yet. */
static void spell_publish_effect(LPEDICT caster, DWORD code, spellTarget_t target) {
    LPEDICT source = target.type == SPELL_TARGET_UNIT ? target.entity : NULL;
    LPCVECTOR2 point = target.type == SPELL_TARGET_POINT ? &target.point : NULL;
    gameEventPointParams_t params = MAKE(gameEventPointParams_t, .edict = caster,
                                         .source = source, .value = (LONG)code, .point = point);

    params.type = EVENT_PLAYER_UNIT_SPELL_EFFECT;
    G_PublishEventWithPoint(&params);
    params.type = EVENT_UNIT_SPELL_EFFECT;
    G_PublishEventWithPoint(&params);
}

/* ---- Per-target-type unified callbacks ---- */

/* Commit a validated unit-target spell at the point where its cast range is reached. */
static void spell_execute_unit_target(spellUnitTargetParams_t const *params) {
    spellTarget_t st = { .type = SPELL_TARGET_UNIT, .entity = params->target };
    abilityitem_t item = { .code = params->code, .ability = params->spell };

    spell_commit(params->caster, params->code, params->level);
    if (params->spell->flags & AB_CHANNEL)
        spell_begin_channel(params->caster, params->code);
    spell_publish_effect(params->caster, params->code, st);
    spell_message(params->caster, A_EXECUTE, &item, &st);
}

/* Ranged target spells are accepted before the caster is in range. Warsmash's
 * CBehaviorTargetSpellBase owns that approach phase and only performs the spell
 * effect once canReach(target, castRange) becomes true. Keep the same separation
 * here: the player's click selects the spell target, while this short-lived
 * server thinker watches the ordinary Move order and commits the spell when the
 * caster reaches authored cast range. Replacing that Move order cancels the
 * pending cast naturally. */
static void spell_unit_target_approach_think(LPEDICT thinker) {
    LPEDICT caster = thinker ? thinker->owner : NULL;
    LPEDICT target = thinker ? thinker->goalentity : NULL;
    DWORD code = thinker ? thinker->class_id : 0;
    ability_t const *spell = S_SpellAbilityForCode(code);
    abilityitem_t item = { .code = code, .ability = spell };
    DWORD level;
    FLOAT range;
    spellTarget_t st;

    if (!caster || !caster->inuse || M_IsDead(caster) || !target || !spell ||
        (spell->target_type != SPELL_TARGET_UNIT && spell->target_type != SPELL_TARGET_UNIT_OR_POINT)) {
        G_FreeEdict(thinker);
        return;
    }
    /* A replacement order is authoritative. If the same spell-owned Move is
     * still active but its target died/disappeared, terminate that approach too. */
    if (caster->goalentity != target || !move_is_active_order_walk(caster)) {
        G_FreeEdict(thinker);
        return;
    }
    if (!S_SpellIsAliveTarget(target)) {
        unit_stand(caster);
        G_FreeEdict(thinker);
        return;
    }

    level = S_SpellLevel(caster, code);
    range = S_SpellRange(code, level);
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
    if (!spell_validate(NULL, caster, code, level, target, range)) {
        unit_stand(caster);
        G_FreeEdict(thinker);
        return;
    }

    spellUnitTargetParams_t params = {
        .caster = caster, .code = code, .level = level, .spell = spell, .target = target
    };
    unit_stand(caster);
    spell_execute_unit_target(&params);
    G_FreeEdict(thinker);
}

/* Start the ordinary walk order used to bring an out-of-range spell target into range. */
static BOOL spell_begin_unit_target_approach(LPEDICT caster, DWORD code, LPEDICT target) {
    LPEDICT thinker;

    if (!caster || !target || (caster->aiflags & AI_IMMOBILE) || S_UnitIsCycloned(caster) ||
        G_UnitStatusLevel(caster, MAKEFOURCC('B', 'E', 'e', 'r')))
        return false;

    order_move(caster, target);
    if (caster->goalentity != target || !move_is_active_order_walk(caster))
        return false;

    thinker = G_Spawn();
    thinker->owner = caster;
    thinker->goalentity = target;
    thinker->class_id = code;
    thinker->think = spell_unit_target_approach_think;
    thinker->freetime = G_Time();
    return true;
}

/* Called when user clicks a target entity for a UNIT-target spell. */
static BOOL spell_unit_target_selected(LPEDICT clent, LPEDICT target) {
    LPEDICT caster = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, 0);
    DWORD level = S_SpellLevel(caster, code);
    FLOAT range = S_SpellRange(code, level);
    ability_t const *spell = S_SpellAbilityForCode(code);
    abilityitem_t item = { .code = code, .ability = spell };
    spellTarget_t st = { .type = SPELL_TARGET_UNIT, .entity = target };

    if (!spell) return false;
    /* Range is intentionally excluded here. An otherwise valid out-of-range
     * target is an accepted order; the caster must walk into cast range. */
    if (!spell_validate(clent, caster, code, level, target, 0.0f)) return false;
    if (!S_SpellAllowsTarget(code, caster, target)) return false;
    if (!S_SpellIsAliveTarget(target)) return false;
    if (!spell_message(caster, A_VALIDATE, &item, &st)) return false;

    if (!S_SpellTargetInRange(caster, target, range))
        return spell_begin_unit_target_approach(caster, code, target);

    spellUnitTargetParams_t params = {
        .caster = caster, .code = code, .level = level, .spell = spell, .target = target
    };
    spell_execute_unit_target(&params);
    return true;
}

/* Called when user clicks a location for a POINT-target spell. */
static BOOL spell_point_target_selected(LPEDICT clent, LPCVECTOR2 point) {
    LPEDICT caster = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, 0);
    DWORD level = S_SpellLevel(caster, code);
    FLOAT range = S_SpellRange(code, level);
    ability_t const *spell = S_SpellAbilityForCode(code);
    abilityitem_t item = { .code = code, .ability = spell };
    spellPointValidateParams_t val = MAKE(spellPointValidateParams_t,
                                          .clent = clent, .caster = caster, .code = code, .level = level,
                                          .point = point, .range = range);

    if (!spell) return false;
    if (!spell_validate_point(&val)) return false;
    spellTarget_t st = { .type = SPELL_TARGET_POINT, .point = *point };
    if (!spell_message(caster, A_VALIDATE, &item, &st)) return false;

    spell_commit(caster, code, level);
    if (spell->flags & AB_CHANNEL)
        spell_begin_channel(caster, code);
    spell_publish_effect(caster, code, st);
    spell_message(caster, A_EXECUTE, &item, &st);
    S_SpellCursorSplat(clent, 0.0f);
    G_SendPointConfirmation(clent, point, false);
    return true;
}

/* No-target (self-cast / instant) execute in-place. */
static void spell_no_target_execute(LPEDICT clent) {
    LPEDICT caster = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, 0);
    DWORD level = S_SpellLevel(caster, code);
    ability_t const *spell = S_SpellAbilityForCode(code);
    abilityitem_t item = { .code = code, .ability = spell };

    if (!spell) return;
    if (!spell_validate(clent, caster, code, level, NULL, 0.0f)) return;
    spellTarget_t st = { .type = SPELL_TARGET_NONE, .entity = NULL };
    if (!spell_message(caster, A_VALIDATE, &item, &st)) return;

    spell_commit(caster, code, level);
    if (spell->flags & AB_CHANNEL) spell_begin_channel(caster, code);
    spell_publish_effect(caster, code, st);
    spell_message(caster, A_EXECUTE, &item, &st);
}

BOOL S_CastNoTargetSpell(LPEDICT caster, DWORD code) {
    DWORD level;
    ability_t const *spell;
    spellTarget_t target = { .type = SPELL_TARGET_NONE };

    if (!caster || !code || !G_UnitAbilityLevel(caster, code) || S_UnitPolymorphed(caster)) return false;
    spell = S_SpellAbilityForCode(code);
    abilityitem_t item = { .code = code, .ability = spell };
    if (!spell || spell->target_type != SPELL_TARGET_NONE || !S_AbilityHasCommand(spell)) return false;
    level = S_SpellLevel(caster, code);
    if (!spell_validate(NULL, caster, code, level, NULL, 0)) return false;
    if (!spell_message(caster, A_VALIDATE, &item, &target)) return false;

    spell_commit(caster, code, level);
    if (spell->flags & AB_CHANNEL) spell_begin_channel(caster, code);
    spell_publish_effect(caster, code, target);
    spell_message(caster, A_EXECUTE, &item, &target);
    return true;
}

BOOL S_CastPointTargetSpell(LPEDICT caster, DWORD code, LPCVECTOR2 point) {
    DWORD level;
    FLOAT range;
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
                                          .point = point, .range = range);
    if (!spell_validate_point(&val)) return false;
    target = MAKE(spellTarget_t, .type = SPELL_TARGET_POINT, .point = *point);
    if (!spell_message(caster, A_VALIDATE, &item, &target)) return false;

    spell_commit(caster, code, level);
    if (spell->flags & AB_CHANNEL) spell_begin_channel(caster, code);
    spell_publish_effect(caster, code, target);
    spell_message(caster, A_EXECUTE, &item, &target);
    return true;
}

/* Autocast and AI orders use the same target and resource contract as a player-selected unit spell. */
BOOL S_CastUnitTargetSpell(LPEDICT caster, DWORD code, LPEDICT unit) {
    DWORD level;
    ability_t const *spell;
    spellTarget_t target = { .type = SPELL_TARGET_UNIT, .entity = unit };

    if (!caster || !unit || !code || !G_UnitAbilityLevel(caster, code) || S_UnitPolymorphed(caster)) return false;
    spell = S_SpellAbilityForCode(code);
    abilityitem_t item = { .code = code, .ability = spell };
    if (!spell || spell->target_type != SPELL_TARGET_UNIT || !S_AbilityHasCommand(spell)) return false;
    level = S_SpellLevel(caster, code);
    if (!spell_validate(NULL, caster, code, level, unit, S_SpellRange(code, level)) ||
        !S_SpellAllowsTarget(code, caster, unit)) return false;
    if (!spell_message(caster, A_VALIDATE, &item, &target)) return false;

    spell_commit(caster, code, level);
    if (spell->flags & AB_CHANNEL) spell_begin_channel(caster, code);
    spell_publish_effect(caster, code, target);
    spell_message(caster, A_EXECUTE, &item, &target);
    return true;
}

BOOL S_IssueUnitTargetSpell(LPEDICT caster, DWORD code, LPEDICT unit) {
    DWORD level;
    FLOAT range;
    ability_t const *spell;
    spellTarget_t target = { .type = SPELL_TARGET_UNIT, .entity = unit };

    if (!caster || !unit || !code || !G_UnitAbilityLevel(caster, code) || S_UnitPolymorphed(caster)) return false;
    spell = S_SpellAbilityForCode(code);
    abilityitem_t item = { .code = code, .ability = spell };
    if (!spell || (spell->target_type != SPELL_TARGET_UNIT &&
                   spell->target_type != SPELL_TARGET_UNIT_OR_POINT) ||
        !S_AbilityHasCommand(spell) || (spell->flags & AB_TOGGLE)) return false;
    level = S_SpellLevel(caster, code);
    range = S_SpellRange(code, level);
    if (!spell_validate(NULL, caster, code, level, unit, 0.0f) ||
        !S_SpellIsAliveTarget(unit) || !S_SpellAllowsTarget(code, caster, unit)) return false;
    if (!spell_message(caster, A_VALIDATE, &item, &target)) return false;
    if (!S_SpellTargetInRange(caster, unit, range))
        return spell_begin_unit_target_approach(caster, code, unit);

    spellUnitTargetParams_t params = {
        .caster = caster, .code = code, .level = level, .spell = spell, .target = unit
    };
    spell_execute_unit_target(&params);
    return true;
}

/* Shared command entry point for all spell abilities.  Sets up the appropriate
 * target-selection UI based on ability_t.target_type, or executes
 * immediately for no-target spells. */
void spell_cmd(LPEDICT clent) {
    LPEDICT caster = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, 0);
    ability_t const *spell = S_SpellAbilityForCode(code);
    abilityitem_t item = { .code = code, .ability = spell };

    if (!spell) {
        fprintf(stderr, "spell_cmd: no executable spell ability for code '%.4s'\n", (LPCSTR)&code);
        return;
    }
    if (!caster) return;

    /* Toggle abilities bypass the normal pipeline. */
    if (spell->flags & AB_TOGGLE) {
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
        UI_AddCancelButton(clent);
        FLOAT area = S_SpellNumber(code, ABILITY_NUMBER_AREA, S_SpellLevel(caster, code));
        S_SpellCursorSplat(clent, area > 0 ? area : 200.0f);
        clent->client->menu.on_location_selected = spell_point_target_selected;
        break;
    }
    case SPELL_TARGET_UNIT_OR_POINT:
        spell_no_target_execute(clent); /* TODO: add unit-or-point fallback via smart-click */
        break;
    }
}
