#include "s_skills.h"

#define HUMAN_AUTOCAST_RADIUS 900.0f // world units; fallback acquisition radius when the spell range is zero
#define BZ_AVATAR_BUFF MAKEFOURCC('B', 'H', 'a', 'v') // rawcode; timed Avatar buff that owns immunity and bonuses
#define BZ_ANTI_MAGIC_SHELL_BUFF MAKEFOURCC('B', 'a', 'm', 's') // rawcode; timed Anti-Magic Shell immunity buff
#define BZ_POLYMORPH MAKEFOURCC('A', 'p', 'l', 'y') // rawcode; stock Polymorph ability code
#define BZ_POLYMORPH_GROUND_SLOT 2 // data slot; authored Ply2 ground morph form
#define BZ_POLYMORPH_FLY_SLOT 3 // data slot; authored Ply3 flying morph form
#define BZ_POLYMORPH_AMPH_SLOT 4 // data slot; authored Ply4 amphibious morph form
#define BZ_POLYMORPH_FLOAT_SLOT 5 // data slot; authored Ply5 floating morph form

typedef struct {
    LPCSTR name;
    DWORD slot;
} polymorphMoveType_t;

static polymorphMoveType_t const polymorph_move_types[] = {
    { "fly", BZ_POLYMORPH_FLY_SLOT },
    { "amph", BZ_POLYMORPH_AMPH_SLOT },
    { "float", BZ_POLYMORPH_FLOAT_SLOT }
};
static DWORD const polymorph_move_types_count = sizeof(polymorph_move_types) / sizeof(polymorph_move_types[0]);

void human_ability_think(LPEDICT thinker);

static LPCSTR human_buff(abilityitem_t const *spell, DWORD level) {
    LPCSTR buff = G_AbilityLevel(spell->code, level)->buffID;
    return buff && strlen(buff) >= 4 ? buff : NULL;
}

static BOOL human_has_status(LPCEDICT ent, DWORD code) { return G_UnitStatusLevel(ent, code) != 0; }

static void human_remove_status(LPEDICT ent, DWORD code) {
    FOR_LOOP(i, MAX_UNIT_STATUSES)
        if (ent->abilstatus[i].level && ent->abilstatus[i].code == code)
            memset(ent->abilstatus + i, 0, sizeof(ent->abilstatus[i]));
}

static void human_status_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    LPCSTR buff = human_buff(spell, level);
    if (!st.entity || !buff) return;
    unit_addtimedstatus(st.entity, buff, level, S_SpellDuration(spell->code, level, G_UnitIsHero(st.entity)));
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, st.entity, NULL, true);
}

static void human_toggle_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    FLOAT duration = S_SpellDuration(spell->code, level, G_UnitIsHero(caster));
    (void)st;
    if (human_has_status(caster, spell->code)) {
        human_remove_status(caster, spell->code); S_HumanStatusExpired(caster, spell->code, level); return;
    }
    unit_addtimedstatus(caster, GetClassName(spell->code), level, duration);
}

/* Retail timed immunity buffs block spell targeting and impacts, independently of physical damage. */
BOOL S_UnitSpellImmune(LPCEDICT unit) {
    return unit && (G_UnitStatusLevel(unit, BZ_AVATAR_BUFF) || G_UnitStatusLevel(unit, BZ_ANTI_MAGIC_SHELL_BUFF));
}

/* Spell impacts recheck immunity because a missile may have launched before Avatar was cast. */
BOOL S_SpellDamage(LPEDICT target, LPEDICT caster, int damage) {
    if (!target || S_UnitSpellImmune(target)) return false;
    T_Damage(target, caster, damage); return true;
}

/* Retail removes the stored deltas and clamps current health instead of subtracting it. */
void S_AvatarExpire(LPEDICT unit) {
    if (!unit || !unit->avatar.level) return;
    unit->temporary_armor_bonus -= unit->avatar.armor; unit->armor_value -= unit->avatar.armor;
    unit->attack1.temporaryDamageBonus -= unit->avatar.damage;
    unit->attack2.temporaryDamageBonus -= unit->avatar.damage;
    unit->temporary_health_bonus -= unit->avatar.health;
    unit->health.max_value = MAX(1.0f, unit->health.max_value - unit->avatar.health);
    unit->health.value = MIN(unit->health.value, unit->health.max_value);
    memset(&unit->avatar, 0, sizeof(unit->avatar));
    human_remove_status(unit, BZ_AVATAR_BUFF);
    G_AddUnitAnimationProperties(unit, "alternate", false); G_InvalidateUnitInfoPanel(unit);
}

/* Reserve the Avatar buff slot before spell_commit spends mana; cooldowns have independent storage. */
static BOOL avatar_validate(LPEDICT caster, spellTarget_t target, abilityitem_t const *spell) {
    (void)spell;
    DWORD slots = 0;
    (void)target; unit_updatestatuses(caster);
    if (!S_SpellIsAliveTarget(caster) || caster->avatar.level) return false;
    FOR_LOOP(i, MAX_UNIT_STATUSES)
        if (!caster->abilstatus[i].level) slots++;
    if (slots >= 1) return true;
    fprintf(stderr, "WC3 Avatar: status capacity exhausted for unit %u\n", caster->s.number); return false;
}

/* CAbilityAvatar creates BHav, then CBuffAvatar applies the authored A/B/C deltas. */
static void avatar_execute(LPEDICT caster, spellTarget_t target, abilityitem_t const *spell) {
    DWORD rank = S_SpellLevel(caster, spell->code);
    (void)target;
    if (caster->avatar.level) return;
    unit_addtimedstatus(caster, "BHav", rank, S_SpellDuration(spell->code, rank, false));
    if (!G_UnitStatusLevel(caster, BZ_AVATAR_BUFF)) {
        fprintf(stderr, "WC3 Avatar: failed to allocate BHav status\n"); return;
    }
    caster->avatar.level = rank; caster->avatar.armor = S_SpellData(spell->code, rank, 1);
    caster->avatar.health = S_SpellData(spell->code, rank, 2); caster->avatar.damage = (LONG)S_SpellData(spell->code, rank, 3);
    caster->temporary_armor_bonus += caster->avatar.armor; caster->armor_value += caster->avatar.armor;
    caster->attack1.temporaryDamageBonus += caster->avatar.damage;
    caster->attack2.temporaryDamageBonus += caster->avatar.damage;
    caster->temporary_health_bonus += caster->avatar.health;
    caster->health.max_value = MAX(1.0f, caster->health.max_value + caster->avatar.health);
    caster->health.value = MIN(caster->health.max_value, caster->health.value + MAX(0.0f, caster->avatar.health));
    G_AddUnitAnimationProperties(caster, "alternate", true); G_InvalidateUnitInfoPanel(caster);
}

/* BuffID orders the caster and victim markers; only the victim marker owns movement/attack locks. */
static DWORD shackles_buff(DWORD code, DWORD rank) {
    LPCSTR buffs = G_AbilityLevel(code, rank)->buffID;
    char source[5], target[5];
    if (buffs && sscanf(buffs, "%4[^,],%4s", source, target) == 2) return FS_SLKKey(target);
    fprintf(stderr, "WC3 Shackles: missing caster/target buff pair for %08x\n", code);
    return 0;
}

static BOOL aerial_shackles_validate(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    return st.entity && st.entity->targtype == TARG_AIR && S_SpellIsEnemy(caster, st.entity) &&
        shackles_buff(spell->code, S_SpellLevel(caster, spell->code));
}

static BOOL control_magic_validate(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    return st.entity && st.entity->owner && S_SpellIsEnemy(caster, st.entity) &&
           caster->mana.value >= st.entity->health.value * S_SpellData(spell->code, level, 2);
}

static void control_magic_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    caster->mana.value -= st.entity->health.value * S_SpellData(spell->code, level, 2);
    G_SetUnitPlayer(st.entity, caster->s.player); st.entity->owner = caster; st.entity->combatentity = NULL;
    if (st.entity->stand) st.entity->stand(st.entity);
}

static BOOL cloud_validate(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    (void)spell;
    return st.entity && G_UnitIsBuilding(st.entity->class_id) && st.entity->attack1.type != ATK_NONE &&
           S_SpellIsEnemy(caster, st.entity);
}

static BOOL inner_fire_validate(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    (void)spell;
    return st.entity && S_SpellIsFriend(caster, st.entity);
}

static BOOL heal_validate(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    (void)spell;
    return st.entity && st.entity->targtype != TARG_MECHANICAL && S_SpellIsFriend(caster, st.entity) &&
           st.entity->health.value < st.entity->health.max_value;
}

static void heal_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    S_SpellHeal(st.entity, S_SpellData(spell->code, S_SpellLevel(caster, spell->code), 1));
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, st.entity, NULL, true);
}

static BOOL slow_validate(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    (void)spell;
    return st.entity && S_SpellIsEnemy(caster, st.entity);
}

static BOOL invisibility_validate(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    (void)spell;
    return st.entity && S_SpellIsFriend(caster, st.entity);
}

static void invisibility_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    human_status_execute(caster, st, spell);
    st.entity->s.renderfx |= RF_HIDDEN;
}

/* Expose the authoritative runtime flag used by order validation and JASS. */
BOOL S_UnitPolymorphed(LPCEDICT unit) {
    return unit && unit->polymorph.active;
}

/* Select the authored Ply2-Ply5 form from the target's movement class. */
static DWORD polymorph_form_type(LPCEDICT target, DWORD level, DWORD code) {
    LPCSTR movetp;
    DWORD data_slot = BZ_POLYMORPH_GROUND_SLOT;

    if (!target || !target->data.UnitData) return 0;
    movetp = target->data.UnitData->moveTypeName;
    if (movetp) {
        FOR_LOOP(i, polymorph_move_types_count)
            if (!strcmp(movetp, polymorph_move_types[i].name)) {
                data_slot = polymorph_move_types[i].slot;
                break;
            }
    }
    /* AbilityData stores Ply1..Ply5 in DataA..DataE.  The typed FOURCC view
     * deliberately keeps the first rawcode from Warcraft's unit-list string;
     * stock Aply uses one morph unit for each movement class. */
    return S_SpellDataId(code, level, data_slot);
}

/* Reject invalid targets and authored morph forms before spell resources commit. */
static BOOL polymorph_validate(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level, max_creep_level, form_type;
    UnitBalance_t const *balance;

    if (!st.entity || !S_SpellIsEnemy(caster, st.entity) || G_UnitIsHero(st.entity) ||
        st.entity->summon_ability || (st.entity->aiflags & AI_ILLUSION) ||
        st.entity->targtype == TARG_MECHANICAL) return false;
    level = S_SpellLevel(caster, spell->code);
    max_creep_level = (DWORD)MAX(0.0f, S_SpellData(spell->code, level, 1)); /* Ply1 */
    balance = st.entity->data.UnitBalance;
    if (st.entity->s.player == PLAYER_NEUTRAL_AGGRESSIVE && max_creep_level && balance &&
        balance->level > (LONG)max_creep_level) return false;
    form_type = polymorph_form_type(st.entity, level, spell->code);
    if (!form_type) {
        fprintf(stderr, "WC3 Polymorph: no authored morph form for target %08x\n", st.entity->class_id);
        return false;
    }
    if (!G_UnitUI(form_type)->modelFile) {
        fprintf(stderr, "WC3 Polymorph: morph form %08x has no model data\n", form_type);
        return false;
    }
    return true;
}

/* Restore the target's saved presentation and movement state when Polymorph ends. */
void S_PolymorphRemove(LPEDICT unit) {
    LPGAMECLIENT client;

    if (!unit || !unit->polymorph.active) return;
    unit->s.model = unit->polymorph.original_model;
    unit->s.scale = unit->polymorph.original_scale;
    unit->unitinfo.MoveSpeed = unit->polymorph.original_move_speed;
    memset(&unit->polymorph, 0, sizeof(unit->polymorph));
    unit->animation = NULL;
    if (!M_IsDead(unit)) {
        G_ClearUnitOrderQueue(unit);
        unit_leavecombat(unit);
        unit->goalentity = NULL;
        unit->secondarygoal = NULL;
        unit_stand(unit);
    }
    client = G_GetPlayerClientByNumber(unit->s.player);
    if (client) G_InvalidateCommands(client);
    G_InvalidateUnitInfoPanel(unit);
    G_InvalidateUnitPortrait(unit);
}

/* Apply the authored morph presentation while preserving the target edict and stats. */
static void polymorph_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level, form_type, buff_code = 0;
    LPCSTR buff;
    UnitUI_t const *ui;
    UnitBalance_t const *balance;
    PATHSTR model_filename;
    int model;
    FLOAT duration;

    if (!st.entity) return;
    level = S_SpellLevel(caster, spell->code);
    form_type = polymorph_form_type(st.entity, level, spell->code);
    buff = human_buff(spell, level);
    if (!form_type) {
        fprintf(stderr, "WC3 Polymorph: no authored morph form for target %08x\n", st.entity->class_id);
        return;
    }
    if (!buff) {
        fprintf(stderr, "WC3 Polymorph: ability %08x has no configured buff\n", spell->code);
        return;
    }
    memcpy(&buff_code, buff, MIN((size_t)4, strlen(buff)));
    ui = G_UnitUI(form_type);
    balance = G_UnitBalance(form_type);
    if (!ui->modelFile) {
        fprintf(stderr, "WC3 Polymorph: morph form %08x has no model data\n", form_type);
        return;
    }
    G_NormalizeModelFilename(ui->modelFile, model_filename, sizeof(model_filename));
    model = G_RegisterModel(model_filename);
    if (!model) {
        fprintf(stderr, "WC3 Polymorph: failed to register model '%s' for form %08x\n",
                model_filename, form_type);
        return;
    }

    duration = S_SpellDuration(spell->code, level, false);
    unit_addtimedstatus(st.entity, buff, level, duration);
    if (!G_UnitStatusLevel(st.entity, buff_code)) {
        fprintf(stderr, "WC3 Polymorph: failed to apply buff %08x to target %08x\n",
                buff_code, st.entity->class_id);
        return;
    }

    if (!st.entity->polymorph.active) {
        st.entity->polymorph.original_model = st.entity->s.model;
        st.entity->polymorph.original_scale = st.entity->s.scale;
        st.entity->polymorph.original_move_speed = st.entity->unitinfo.MoveSpeed;
    }
    st.entity->polymorph.active = true;
    st.entity->polymorph.ability = spell->code;
    st.entity->polymorph.buff = buff_code;
    st.entity->polymorph.form_type = form_type;
    st.entity->s.model = model;
    st.entity->s.scale = ui->modelScale > 0.0f ? ui->modelScale : 1.0f;
    if (balance->speed > 0.0f) st.entity->unitinfo.MoveSpeed = balance->speed;

    /* Polymorph interrupts actions that require the original unit's attacks or
     * command abilities.  The transformed unit remains the same edict/JASS
     * handle and may receive fresh movement orders while the buff is active. */
    G_ClearUnitOrderQueue(st.entity);
    S_SpellCancelChannel(st.entity);
    unit_leavecombat(st.entity);
    st.entity->goalentity = NULL;
    st.entity->secondarygoal = NULL;
    st.entity->animation = NULL;
    unit_stand(st.entity);
    {
        LPGAMECLIENT client = G_GetPlayerClientByNumber(st.entity->s.player);
        if (client) G_InvalidateCommands(client);
    }
    G_InvalidateUnitInfoPanel(st.entity);
    G_InvalidateUnitPortrait(st.entity);
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, st.entity, NULL, true);
}

static void aerial_shackles_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    LPEDICT thinker = S_SpellChannelThinker(caster, spell->code);
    thinker->goalentity = st.entity; thinker->channel.target_spawn_time = st.entity->spawn_time;
    thinker->resources = shackles_buff(spell->code, level);
    thinker->damage = (DWORD)S_SpellData(spell->code, level, 1); thinker->spawn_time = G_Time() +
        (DWORD)(S_SpellDuration(spell->code, level, G_UnitIsHero(st.entity)) * 1000.0f);
    thinker->think = human_ability_think;
    unit_addtimedstatus(st.entity, GetClassName(thinker->resources), level, S_SpellDuration(spell->code, level, G_UnitIsHero(st.entity)));
    human_ability_think(thinker);
}

/* A cancelled cast must release its lock without erasing a replacement cast's lock on the same victim. */
static void shackles_end(LPEDICT thinker) {
    LPEDICT target = thinker->goalentity;
    BOOL retained = false;
    if (target && target->inuse && target->spawn_time == thinker->channel.target_spawn_time) {
        FILTER_EDICTS(other, other != thinker && other->think == human_ability_think && other->goalentity == target &&
            other->resources == thinker->resources && other->channel.target_spawn_time == target->spawn_time) {
            if (S_SpellChannelActive(other)) { retained = true; break; }
        }
        if (!retained) human_remove_status(target, thinker->resources);
    }
    S_SpellEndChannel(thinker);
}

void human_ability_think(LPEDICT thinker) {
    DWORD now = G_Time();
    if (S_AbilityItem(thinker->class_id).ability->proc == CAbilityFlare) {
        if (now >= thinker->spawn_time || !thinker->owner || !thinker->owner->inuse) { G_FreeEdict(thinker); return; }
        G_FowSetStateRadius(&(FOGWRITE){ thinker->owner->s.player, WC3_FOG_STATE_VISIBLE, true }, &thinker->s.origin2,
                            S_SpellNumber(thinker->class_id, ABILITY_NUMBER_AREA, 1));
        return;
    }
    if (now >= thinker->spawn_time || !S_SpellChannelActive(thinker) ||
        !S_SpellIsAliveTarget(thinker->goalentity) ||
        thinker->goalentity->spawn_time != thinker->channel.target_spawn_time) {
        shackles_end(thinker); return;
    }
    if (!thinker->freetime || now >= thinker->freetime) {
        S_SpellDamage(thinker->goalentity, thinker->owner, thinker->damage); thinker->freetime = now + 1000;
    }
}

static void spell_steal_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    LPEDICT receiver = NULL;
    heroabilitystatus_t stolen = {0};
    DWORD level = S_SpellLevel(caster, spell->code);
    FLOAT area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        if (st.entity->abilstatus[i].level && st.entity->abilstatus[i].timestamp) {
            stolen = st.entity->abilstatus[i];
            S_HumanStatusExpired(st.entity, stolen.code, stolen.level);
            memset(st.entity->abilstatus + i, 0, sizeof(st.entity->abilstatus[i]));
            break;
        }
    }
    if (!stolen.level) return;
    FILTER_EDICTS(unit, unit != st.entity && S_SpellIsAliveTarget(unit) && S_SpellIsFriend(caster, unit) &&
                  Vector2_distance(&unit->s.origin2, &st.entity->s.origin2) <= area) { receiver = unit; break; }
    if (!receiver) receiver = caster;
    unit_addtimedstatus(receiver, (LPCSTR)&stolen.code, stolen.level,
                        stolen.timestamp > G_Time() ? (stolen.timestamp - G_Time()) / 1000.0f : 0.0f);
}

static BOOL human_autocast_acquire(LPEDICT caster, DWORD code, BOOL friendly, BOOL wounded) {
    LPEDICT best = NULL;
    FLOAT range = S_SpellRange(code, S_SpellLevel(caster, code));
    FLOAT best_distance = FLT_MAX;
    if (range <= 0.0f) range = HUMAN_AUTOCAST_RADIUS;
    FILTER_EDICTS(target, target != caster && S_SpellIsAliveTarget(target)) {
        FLOAT distance;
        if (friendly != S_SpellIsFriend(caster, target)) continue;
        if (wounded && target->health.value >= target->health.max_value) continue;
        if (!S_SpellAllowsTarget(code, caster, target)) continue;
        distance = Vector2_distance(&target->s.origin2, &caster->s.origin2);
        if (distance <= range && distance < best_distance) { best = target; best_distance = distance; }
    }
    return best && S_CastUnitTargetSpell(caster, code, best);
}

/* The message selects the union member: boolean toggles must never be decoded as target pointers. */
#define BZ_HUMAN_AUTOCAST_SPELL(NAME, VALIDATE, EXECUTE, FRIENDLY, WOUNDED) \
    BZ_ABILITY_PROC(C##NAME) { \
        spellTarget_t target = (msg == A_VALIDATE || msg == A_EXECUTE) && call && call->target ? \
            *call->target : MAKE(spellTarget_t, .type = SPELL_TARGET_NONE); \
        DWORD code = call && call->item ? call->item->code : 0; \
        switch (msg) { \
        case A_VALIDATE: return VALIDATE; \
        case A_EXECUTE: EXECUTE(ent, target, call ? call->item : NULL); return true; \
        case A_AUTOCAST_ON: return ent && ent->autocast_code == code; \
        case A_AUTOCAST_SET: return true; \
        case A_AUTOCAST_ACQUIRE: return human_autocast_acquire(ent, code, FRIENDLY, WOUNDED); \
        default: return CAbilitySimpleSpell(ent, msg, call); \
        } \
    }

/* Name=Aerial Shackles
 * Ubertip="Magically binds a target enemy air unit, so that it cannot move or attack and takes <Amls,DataA1> damage per second. Lasts <Amls,Dur1> seconds."
 */
BZ_VALIDATED_SPELL_PROC(AbilityMagicLeash, aerial_shackles_validate, aerial_shackles_execute)
/* Name=Control Magic
 * Ubertip="Takes control of an enemy summoned unit. The mana cost is <Acmg,DataB1,%>% of the summoned unit's current hit points."
 */
BZ_VALIDATED_SPELL_PROC(AbilityControlMagic, control_magic_validate, control_magic_execute)
/* Name=Magic Defense; Untip=Stop Magic Defense */
BZ_SIMPLE_SPELL_PROC(AbilityMagicDefense) { human_toggle_execute(caster, st, spell); }
/* Name=Spell Steal; Untip="Right-click to activate auto-casting." */
BZ_HUMAN_AUTOCAST_SPELL(AbilitySpellSteal, true, spell_steal_execute, false, false)
/* Name=Cloud; Ubertip="Cast on enemy buildings with ranged attacks to stop the buildings from attacking. Lasts <Aclf,Dur1> seconds." */
BZ_VALIDATED_SPELL_PROC(AbilityCloudOfFog, cloud_validate, human_status_execute)
/* Name=Defend; Untip=Stop Defend */
BZ_SIMPLE_SPELL_PROC(AbilityDefend) { human_toggle_execute(caster, st, spell); }
/* Name=Flare; Ubertip="Launches a Dwarven flare above a target point, which reveals that area for <Afla,Dur1> seconds." */
BZ_SIMPLE_SPELL_PROC(AbilityFlare) {
    DWORD level = S_SpellLevel(caster, spell->code);
    LPEDICT thinker = G_Spawn();
    thinker->owner = caster; thinker->class_id = spell->code; thinker->s.origin2 = st.point;
    thinker->spawn_time = G_Time() + (DWORD)(S_SpellDuration(spell->code, level, false) * 1000.0f);
    thinker->think = human_ability_think; human_ability_think(thinker);
}
/* Name=Inner Fire; Untip="Right-click to activate auto-casting." */
BZ_HUMAN_AUTOCAST_SPELL(AbilityInnerFire, inner_fire_validate(ent, target, call ? call->item : NULL), human_status_execute, true, false)
/* Name=Dispel Magic; Ubertip="Removes all buffs from units in a target area. Deals <Adis,DataB1> damage to summoned units." */
BZ_SIMPLE_SPELL_PROC(AbilityDispelMagic) {
    DWORD level = S_SpellLevel(caster, spell->code);
    FLOAT area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && Vector2_distance(&target->s.origin2, &st.point) <= area) {
        FOR_LOOP(i, MAX_UNIT_STATUSES) {
            if (target->abilstatus[i].level && target->abilstatus[i].timestamp) {
                DWORD code = target->abilstatus[i].code, status_level = target->abilstatus[i].level;
                S_HumanStatusExpired(target, code, status_level);
                memset(target->abilstatus + i, 0, sizeof(target->abilstatus[i]));
            }
        }
        if (target->owner) S_SpellDamage(target, caster, (int)S_SpellData(spell->code, level, 2));
    }
}
/* Name=Heal; Ubertip="Heals a target friendly non-mechanical wounded unit for <Ahea,DataA1> hit points." */
BZ_HUMAN_AUTOCAST_SPELL(AbilityHeal, heal_validate(ent, target, call ? call->item : NULL), heal_execute, true, true)
/* Name=Slow; Untip="Right-click to activate auto-casting." */
BZ_HUMAN_AUTOCAST_SPELL(AbilitySlow, slow_validate(ent, target, call ? call->item : NULL), human_status_execute, false, false)
/* Name=Invisibility; Ubertip="Makes a unit invisible. If the unit attacks, uses an ability or casts a spell, it will become visible." */
BZ_VALIDATED_SPELL_PROC(AbilityInvisibility, invisibility_validate, invisibility_execute)
/* Name=Polymorph; Ubertip="Turns a target enemy unit into a sheep. Cannot be cast on Heroes. Lasts <Aply,Dur1> seconds." */
BZ_VALIDATED_SPELL_PROC(AbilityPolymorph, polymorph_validate, polymorph_execute)
/* Name=Avatar */
BZ_ABILITY_PROC(CAbilityAvatar) {
    spellTarget_t target = (msg == A_VALIDATE || msg == A_EXECUTE) && call && call->target ?
        *call->target : MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
    switch (msg) {
    case A_VALIDATE: return avatar_validate(ent, target, call ? call->item : NULL);
    case A_EXECUTE: avatar_execute(ent, target, call ? call->item : NULL); return true;
    case A_DISABLE: S_AvatarExpire(ent); return true;
    default: return CAbilitySimpleSpell(ent, msg, call);
    }
}

BOOL S_HumanCanAttack(LPCEDICT unit) {
    return unit && !human_has_status(unit, MAKEFOURCC('B','m','l','t')) &&
           !human_has_status(unit, MAKEFOURCC('B','c','l','f')) && !S_UnitPolymorphed(unit);
}

FLOAT S_HumanMoveFactor(LPCEDICT unit) {
    DWORD level;
    FLOAT factor = 1.0f;
    if ((level = G_UnitStatusLevel(unit, MAKEFOURCC('B','s','l','o')))) factor *= 1.0f - S_SpellData(MAKEFOURCC('A','s','l','o'), level, 1);
    if ((level = G_UnitStatusLevel(unit, MAKEFOURCC('A','d','e','f')))) factor *= S_SpellData(MAKEFOURCC('A','d','e','f'), level, 3);
    if ((level = G_UnitStatusLevel(unit, MAKEFOURCC('A','m','d','f')))) factor *= S_SpellData(MAKEFOURCC('A','m','d','f'), level, 3);
    if (human_has_status(unit, MAKEFOURCC('B','m','l','t'))) return 0.0f;
    return factor;
}

FLOAT S_HumanArmorBonus(LPCEDICT unit) {
    DWORD level;
    FLOAT bonus = 0.0f;
    if ((level = G_UnitStatusLevel(unit, MAKEFOURCC('B','i','n','f')))) bonus += S_SpellData(MAKEFOURCC('A','i','n','f'), level, 2);
    return bonus;
}

int S_HumanAttackDamage(LPEDICT attacker, LPEDICT target, int damage) {
    DWORD level;
    if ((level = G_UnitStatusLevel(attacker, MAKEFOURCC('B','i','n','f'))))
        damage = (int)(damage * (1.0f + S_SpellData(MAKEFOURCC('A','i','n','f'), level, 1)));
    if ((level = G_UnitAbilityLevel(attacker, MAKEFOURCC('A','f','b','k'))) && target->mana.value > 0.0f) {
        DWORD slot = G_UnitIsHero(target) ? 3 : 1;
        FLOAT drained = MIN(target->mana.value, S_SpellData(MAKEFOURCC('A','f','b','k'), level, slot));
        target->mana.value -= drained; damage += (int)(drained * S_SpellData(MAKEFOURCC('A','f','b','k'), level, slot + 1));
    }
    if ((level = G_UnitAbilityLevel(attacker, MAKEFOURCC('A','f','s','h'))) && target->defense_type <= 2)
        damage += (int)S_SpellData(MAKEFOURCC('A','f','s','h'), level, 3 + target->defense_type);
    if (attacker->attack1.type == ATK_PIERCE && (level = G_UnitStatusLevel(target, MAKEFOURCC('A','d','e','f')))) {
        /* Def6 is a 0..1 probability; the previous integer-percent roll made 0.30 behave as 0.3%. */
        if ((FLOAT)rand() / (FLOAT)RAND_MAX < S_SpellData(MAKEFOURCC('A','d','e','f'), level, 6)) {
            T_Damage(attacker, target, (int)(damage * S_SpellData(MAKEFOURCC('A','d','e','f'), level, 2))); return 0;
        }
        damage = (int)(damage * S_SpellData(MAKEFOURCC('A','d','e','f'), level, 1));
    }
    return damage;
}

void S_HumanAttackSplash(LPEDICT attacker, LPEDICT target, int damage) {
    DWORD flak = G_UnitAbilityLevel(attacker, MAKEFOURCC('A','f','l','k'));
    DWORD barrage = G_UnitAbilityLevel(attacker, MAKEFOURCC('A','r','o','c'));
    DWORD storm = G_UnitAbilityLevel(attacker, MAKEFOURCC('A','s','t','h'));
    FLOAT radius = storm ? attacker->attack1.areaSmall : flak ? S_SpellData(MAKEFOURCC('A','f','l','k'), flak, 2) :
                   barrage ? S_SpellNumber(MAKEFOURCC('A','r','o','c'), ABILITY_NUMBER_AREA, barrage) : 0.0f;
    DWORD count = 0, limit = barrage ? (DWORD)S_SpellData(MAKEFOURCC('A','r','o','c'), barrage, 3) : UINT_MAX;
    if (radius <= 0.0f) return;
    FILTER_EDICTS(other, count < limit && other != target && S_SpellIsAliveTarget(other) && S_SpellIsEnemy(attacker, other) &&
                  (!flak || other->targtype == TARG_AIR) && Vector2_distance(&other->s.origin2, &target->s.origin2) <= radius) {
        FLOAT distance = Vector2_distance(&other->s.origin2, &target->s.origin2), splash = damage;
        if (flak) splash = distance <= S_SpellData(MAKEFOURCC('A','f','l','k'), flak, 1) ?
            S_SpellData(MAKEFOURCC('A','f','l','k'), flak, 3) : S_SpellData(MAKEFOURCC('A','f','l','k'), flak, 4);
        else if (barrage) splash = S_SpellData(MAKEFOURCC('A','r','o','c'), barrage, 1);
        else if (distance > attacker->attack1.areaMedium) splash *= attacker->attack1.factorSmall;
        else if (distance > attacker->attack1.areaFull) splash *= attacker->attack1.factorMedium;
        T_Damage(other, attacker, (int)MAX(1.0f, splash)); count++;
    }
}

void S_HumanBreakInvisibility(LPEDICT unit) {
    if (!unit || !human_has_status(unit, MAKEFOURCC('B','i','n','v'))) return;
    human_remove_status(unit, MAKEFOURCC('B','i','n','v')); unit->s.renderfx &= ~RF_HIDDEN;
}

void S_HumanStatusExpired(LPEDICT unit, DWORD code, DWORD level) {
    (void)level;
    if (!unit) return;
    if (code == MAKEFOURCC('B','i','n','v')) unit->s.renderfx &= ~RF_HIDDEN;
    if (code == BZ_AVATAR_BUFF) S_AvatarExpire(unit);
    if (unit->polymorph.active && code == unit->polymorph.buff) S_PolymorphRemove(unit);
}
