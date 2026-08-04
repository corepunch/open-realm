#include "s_skills.h"

#include <ctype.h>

static umove_t spell_effect_birth = { "birth", NULL, G_FreeEdict };

#define DEFAULT_SPELL_AREA_CURSOR "ReplaceableTextures\\Selection\\SpellAreaOfEffect.blp"

/* ---- Unified Spell Pipeline ----

 * All hero/unit spells route through a single cmd entry point (spell_cmd) that
 * reads the ability code, validates mana/cooldown, configures targeting, and
 * dispatches to the spell_info_t::execute callback once a valid target is
 * acquired.  channeled spells (SPELL_CHANNEL) set ent->channel_code and
 * ent->cast_origin; spell_run_frame() enforces movement-cancel for them.
 *
 * Design mirrors:
 *   - WarSmash: CAbilitySpellBase with target-type dispatch
 *   - WoW: data-driven spell table + cast state machine (Wow_RunSpellCast)
 *   - Quake2: ability_t → cmd function pointer dispatch */

static LPCSTR S_SpellThemeString(LPCSTR key, LPCSTR def) {
    LPCSTR value = NULL;

    if (key && !strstr(key, "\\") && game.config.theme) {
        value = FS_FindSheetCell(game.config.theme, "Default", key);
    }
    return value ? value : def;
}

void S_SpellCodeString(DWORD code, LPSTR out) {
    memcpy(out, &code, 4);
    out[4] = '\0';
}

DWORD S_SpellCurrentCode(LPEDICT clent, DWORD fallback) {
    DWORD code = clent && clent->client ? clent->client->menu.ability_code : 0;
    return code ? code : fallback;
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

FLOAT S_SpellNumber(DWORD code, LPCSTR field, DWORD level) {
    char code_string[5];
    char field_string[16];
    LPCSTR value;
    size_t len;

    if (!field || !*field) {
        return 0;
    }

    S_SpellCodeString(code, code_string);
    len = strlen(field);
    if (level > 0 && level <= 3 && len > 0 && !isdigit((unsigned char)field[len - 1])) {
        snprintf(field_string, sizeof(field_string), "%s%u", field, (unsigned)level);
        field = field_string;
    }
    value = game.config.abilities ? FS_FindSheetCell(game.config.abilities, code_string, field) : NULL;
    if (!value || !strcmp(value, "-")) {
        return 0;
    }
    return atof(value);
}

LPCSTR S_SpellString(DWORD code, LPCSTR field, DWORD level) {
    char code_string[5];
    char field_string[16];
    LPCSTR value;
    size_t len;

    if (!field || !*field) {
        return NULL;
    }

    S_SpellCodeString(code, code_string);
    len = strlen(field);
    if (level > 0 && level <= 3 && len > 0 && !isdigit((unsigned char)field[len - 1])) {
        snprintf(field_string, sizeof(field_string), "%s%u", field, (unsigned)level);
        field = field_string;
    }
    value = game.config.abilities ? FS_FindSheetCell(game.config.abilities, code_string, field) : NULL;
    if (!value || !strcmp(value, "-") || !strcmp(value, "_")) {
        return NULL;
    }
    return value;
}

/* Ability "Data" columns in AbilityData.slk are named Data<Letter><Level> —
 * e.g. DataA1, DataB1 … DataE3 — where the letter (A=1 … I=9) selects the
 * parameter and the trailing digit the ability level.  (Non-Data fields such as
 * Cool/Cost/Rng have no letter dimension, so S_SpellNumber appends just the
 * level.)  index is 1-based: 1=DataA, 5=DataE, etc. */
FLOAT S_SpellData(DWORD code, DWORD level, DWORD index) {
    char field[16];

    level = MAX(1, MIN(level, 4));
    index = MAX(1, MIN(index, 9));
    snprintf(field, sizeof(field), "Data%c%u", 'A' + (int)(index - 1), (unsigned)level);
    return S_SpellNumber(code, field, 0);
}

DWORD S_SpellUnitId(DWORD code, DWORD level) {
    LPCSTR value;

    level = MAX(1, MIN(level, 3));
    value = S_SpellString(code, "UnitID", level);
    if (!value || strlen(value) < 4) {
        return 0;
    }
    return *((DWORD const *)value);
}

FLOAT S_SpellRange(DWORD code, DWORD level) {
    return S_SpellNumber(code, "Rng", level);
}

FLOAT S_SpellDuration(DWORD code, DWORD level, BOOL hero) {
    return S_SpellNumber(code, hero ? "HeroDur" : "Dur", level);
}

BOOL S_SpellCooldownReady(LPEDICT caster, DWORD code) {
    DWORD now;

    if (!caster) {
        return false;
    }
    now = gi.GetTime();
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        heroabilitystatus_t const *status = caster->abilstatus + i;
        if (status->level && status->code == code && status->timestamp > now) {
            return false;
        }
    }
    return true;
}

/* Fraction of an ability's cooldown still remaining for this caster: ~1.0 just
 * after it was used, decaying to 0.0 when it becomes ready again.  Returns 0 if
 * the ability is off cooldown or has none.  Drives the command-card cooldown
 * shade (the darkened icon while an ability recharges). */
FLOAT S_SpellCooldownFraction(LPEDICT caster, DWORD code, DWORD level) {
    DWORD now;

    if (!caster) {
        return 0.0f;
    }
    now = gi.GetTime();
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        heroabilitystatus_t const *status = caster->abilstatus + i;
        if (status->level && status->code == code && status->timestamp > now) {
            FLOAT const total = S_SpellNumber(code, "Cool", level ? level : status->level);
            if (total <= 0.0f) {
                return 0.0f;
            }
            return MIN(1.0f, (FLOAT)(status->timestamp - now) / (total * 1000.0f));
        }
    }
    return 0.0f;
}

void S_SpellStartCooldown(LPEDICT caster, DWORD code, DWORD level) {
    FLOAT cooldown;
    DWORD now;
    heroabilitystatus_t *slot = NULL;

    if (!caster) {
        return;
    }
    cooldown = S_SpellNumber(code, "Cool", level);
    if (cooldown <= 0) {
        return;
    }

    now = gi.GetTime();
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        heroabilitystatus_t *status = caster->abilstatus + i;
        if (status->level && status->code == code) {
            slot = status;
            break;
        }
        if (!status->level && !slot) {
            slot = status;
        }
    }
    if (!slot) {
        return;
    }
    slot->code = code;
    slot->level = level ? level : 1;
    slot->timestamp = now + (DWORD)(cooldown * 1000.0f);
}

BOOL S_SpellSpendMana(LPEDICT caster, DWORD code, DWORD level) {
    FLOAT cost;

    if (!caster) {
        return false;
    }
    cost = S_SpellNumber(code, "Cost", level);
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
    cost = S_SpellNumber(code, "Cost", level);
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
    if (!caster || !target || caster->s.player == target->s.player) {
        return false;
    }
    if (caster->s.player >= MAX_PLAYERS || target->s.player >= MAX_PLAYERS) {
        return false;
    }
    if (level.mapinfo) {
        playerType_t type = level.mapinfo->players[target->s.player].playerType;
        if (type == kPlayerTypeNone) {
            return false;
        }
    }
    return level.alliances[caster->s.player][target->s.player] == 0;
}

BOOL S_SpellIsFriend(LPEDICT caster, LPEDICT target) {
    if (!caster || !target) {
        return false;
    }
    if (caster->s.player == target->s.player) {
        return true;
    }
    if (caster->s.player >= MAX_PLAYERS || target->s.player >= MAX_PLAYERS) {
        return false;
    }
    return level.alliances[caster->s.player][target->s.player] != 0;
}

BOOL S_SpellAllowsTarget(DWORD code, LPEDICT caster, LPEDICT target) {
    LPCSTR targets;

    if (!S_SpellIsAliveTarget(target)) {
        return false;
    }
    targets = S_SpellString(code, "targs", 0);
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
    target->health.value = MIN(target->health.max_value, target->health.value + amount);
}

void S_SpellSpawnTargetArt(LPEDICT target, LPCSTR art) {
    LPEDICT effect;

    if (!target || !art || !*art) {
        return;
    }

    effect = G_Spawn();
    effect->s.origin = target->s.origin;
    effect->s.angle = target->s.angle;
    effect->s.model = G_RegisterModel(art);
    effect->goalentity = target;
    effect->movetype = MOVETYPE_LINK;
    effect->think = M_MoveFrame;
    unit_setmove(effect, &spell_effect_birth);
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
    return caster && caster->channel_code != 0;
}

void S_SpellCancelChannel(LPEDICT caster) {
    if (!caster || !caster->channel_code) {
        return;
    }
    caster->channel_code = 0;
    if (caster->stand) {
        caster->stand(caster);
    }
}

/* ---- Unified Spell Pipeline ---- */

/* Per-frame channel enforcement: if the caster has moved from cast_origin,
 * cancel the channel.  Called from G_RunEntity. */
void spell_run_frame(LPEDICT ent) {
    if (!ent->channel_code)
        return;

    /* Stun or death interrupts channel. */
    if (ent->stunned || M_IsDead(ent)) {
        S_SpellCancelChannel(ent);
        return;
    }

    /* Movement cancel: caster moved from the position where channel began. */
    if (fabsf(ent->s.origin.x - ent->cast_origin.x) > 0.5f ||
        fabsf(ent->s.origin.y - ent->cast_origin.y) > 0.5f) {
        S_SpellCancelChannel(ent);
        return;
    }
}

/* Shared validation for spell spells: mana, cooldown, and optional range check. */
static BOOL spell_validate(LPEDICT caster, DWORD code, DWORD level, LPEDICT target, FLOAT range) {
    if (!caster)
        return false;
    if (!S_SpellCooldownReady(caster, code))
        return false;
    if (!S_SpellCanPay(caster, code, level))
        return false;
    if (range > 0 && target && !S_SpellTargetInRange(caster, target, range))
        return false;
    return true;
}

/* Shared validation for point-target spells. */
static BOOL spell_validate_point(LPEDICT caster, DWORD code, DWORD level, LPCVECTOR2 point, FLOAT range) {
    if (!caster || !point)
        return false;
    if (!S_SpellCooldownReady(caster, code))
        return false;
    if (!S_SpellCanPay(caster, code, level))
        return false;
    if (range > 0 && Vector2_distance(&caster->s.origin2, point) > range)
        return false;
    return true;
}

/* Start channel: lock caster in place and record the origin for movement-cancel. */
static void spell_begin_channel(LPEDICT caster, DWORD code) {
    caster->channel_code = code;
    caster->cast_origin = caster->s.origin2;
}

/* Pre-execute common work: spend mana, start cooldown. */
static void spell_commit(LPEDICT caster, DWORD code, DWORD level) {
    S_SpellSpendMana(caster, code, level);
    S_SpellStartCooldown(caster, code, level);
}

/* ---- Per-target-type unified callbacks ---- */

/* Called when user clicks a target entity for a UNIT-target spell. */
static BOOL spell_unit_target_selected(LPEDICT clent, LPEDICT target) {
    LPEDICT caster = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, 0);
    DWORD level = S_SpellLevel(caster, code);
    FLOAT range = S_SpellRange(code, level);
    ability_t const *abil = FindAbilityByClassname((LPCSTR)&code);
    spell_info_t const *spell = abil ? abil->spell : NULL;

    if (!spell) return false;
    if (!spell_validate(caster, code, level, target, range)) return false;
    if (!S_SpellAllowsTarget(code, caster, target)) return false;
    if (!S_SpellIsAliveTarget(target)) return false;
    spellTarget_t st = { .type = SPELL_TARGET_UNIT, .entity = target };
    if (spell->validate && !spell->validate(caster, st)) return false;

    spell_commit(caster, code, level);
    if (spell->flags & SPELL_CHANNEL)
        spell_begin_channel(caster, code);
    spell->execute(caster, st, spell);
    return true;
}

/* Called when user clicks a location for a POINT-target spell. */
static BOOL spell_point_target_selected(LPEDICT clent, LPCVECTOR2 point) {
    LPEDICT caster = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, 0);
    DWORD level = S_SpellLevel(caster, code);
    FLOAT range = S_SpellRange(code, level);
    ability_t const *abil = FindAbilityByClassname((LPCSTR)&code);
    spell_info_t const *spell = abil ? abil->spell : NULL;

    if (!spell) return false;
    if (!spell_validate_point(caster, code, level, point, range))
        return false;
    spellTarget_t st = { .type = SPELL_TARGET_POINT, .point = *point };
    if (spell->validate && !spell->validate(caster, st)) return false;

    spell_commit(caster, code, level);
    if (spell->flags & SPELL_CHANNEL)
        spell_begin_channel(caster, code);
    spell->execute(caster, st, spell);
    S_SpellCursorSplat(clent, 0.0f);
    return true;
}

/* No-target (self-cast / instant) execute in-place. */
static void spell_no_target_execute(LPEDICT clent) {
    LPEDICT caster = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, 0);
    DWORD level = S_SpellLevel(caster, code);
    ability_t const *abil = FindAbilityByClassname((LPCSTR)&code);
    spell_info_t const *spell = abil ? abil->spell : NULL;

    if (!spell) return;
    if (!S_SpellCooldownReady(caster, code)) return;
    if (!S_SpellCanPay(caster, code, level)) return;
    spellTarget_t st = { .type = SPELL_TARGET_NONE, .entity = NULL };
    if (spell->validate && !spell->validate(caster, st)) return;

    spell_commit(caster, code, level);
    spell->execute(caster, st, spell);
}

/* Shared command entry point for all spell abilities.  Sets up the appropriate
 * target-selection UI based on spell_info_t::target_type, or executes
 * immediately for no-target spells. */
void spell_cmd(LPEDICT clent) {
    LPEDICT caster = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, 0);
    ability_t const *abil = FindAbilityByClassname((LPCSTR)&code);
    spell_info_t const *spell = abil ? abil->spell : NULL;

    if (!spell) {
        fprintf(stderr, "spell_cmd: no spell_info for code '%.4s'\n", (LPCSTR)&code);
        return;
    }

    /* Toggle abilities bypass the normal pipeline. */
    if (spell->flags & SPELL_TOGGLE) {
        spell->execute(caster, (spellTarget_t){ .type = SPELL_TARGET_NONE }, spell);
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
        FLOAT area = S_SpellNumber(code, "Area", S_SpellLevel(caster, code));
        S_SpellCursorSplat(clent, area > 0 ? area : 200.0f);
        clent->client->menu.on_location_selected = spell_point_target_selected;
        break;
    }
    case SPELL_TARGET_UNIT_OR_POINT:
        spell_no_target_execute(clent); /* TODO: add unit-or-point fallback via smart-click */
        break;
    }
}
