#include "s_skills.h"

/* Acpf and Aetf are reversible no-target unit morphs, not target Banish.
 * The authored UnitAbilities for both endpoints contains Acpf. We keep the
 * model change on the original edict and let the shared cast path own mana,
 * cooldown, validation, ability effects and command errors. */
#define SPIRIT_WALKER_ETHEREAL MAKEFOURCC('o','s','p','m')
#define SPIRIT_WALKER_CORPOREAL MAKEFOURCC('o','s','p','w')
#define SPIRIT_WALKER_CORPOREAL_SPELL MAKEFOURCC('A','c','p','f')
#define SPIRIT_WALKER_ETHEREAL_SPELL MAKEFOURCC('A','e','t','f')

cstring_t const spirit_walker_form_orders[] = {
    "corporealform", "uncorporealform", "etherealform", "unetherealform", NULL
};

static uint32_t spirit_walker_form_code(edict_t const *unit) {
    if (!unit) return 0;
    if (unit->class_id == SPIRIT_WALKER_ETHEREAL) return SPIRIT_WALKER_CORPOREAL_SPELL;
    if (unit->class_id == SPIRIT_WALKER_CORPOREAL) return SPIRIT_WALKER_ETHEREAL_SPELL;
    return 0;
}

static void spirit_walker_morph_finished(edict_t *unit) {
    if (unit) unit_stand(unit);
}

static umove_t spirit_walker_morph = {
    "spell morph", NULL, spirit_walker_morph_finished, CAbilitySpiritWalkerForm
};

BZ_ABILITY_PROC(CAbilitySpiritWalkerForm) {
    uint32_t const active_code = spirit_walker_form_code(ent);
    switch (msg) {
    case A_COMMAND: {
        gameClient_t *client;
        uint32_t subgroup;
        bool any = false;
        if (!ent || !call || !call->entity || !(client = call->entity->client) || !active_code) return false;
        subgroup = ent->class_id;
        FOR_CONTROLLABLE_SELECTED_UNITS(client, unit) {
            if (unit->class_id != subgroup || !G_UnitAbilityLevel(unit, active_code)) continue;
            if (S_CastNoTargetSpell(unit, active_code)) any = true;
        }
        if (any) Get_Commands_f(call->entity);
        return any;
    }
    case A_ORDER: {
        cstring_t order = call ? call->order : NULL;
        if (!active_code || !order) return false;
        if (active_code == SPIRIT_WALKER_CORPOREAL_SPELL &&
            strcmp(order, "corporealform") && strcmp(order, "unetherealform")) return false;
        if (active_code == SPIRIT_WALKER_ETHEREAL_SPELL &&
            strcmp(order, "etherealform") && strcmp(order, "uncorporealform")) return false;
        return S_CastNoTargetSpell(ent, active_code);
    }
    case A_VALIDATE: {
        /* S_CastNoTargetSpell commits mana/cooldown before A_EXECUTE.
         * Missing custom-map endpoints must fail before that commit. */
        uint32_t const target = active_code == SPIRIT_WALKER_CORPOREAL_SPELL ?
            SPIRIT_WALKER_CORPOREAL : SPIRIT_WALKER_ETHEREAL;
        return active_code && call && call->item && call->item->code == active_code &&
               G_UnitUI(target)->modelFile != NULL;
    }
    case A_EXECUTE: {
        uint32_t const target = active_code == SPIRIT_WALKER_CORPOREAL_SPELL ?
            SPIRIT_WALKER_CORPOREAL : SPIRIT_WALKER_ETHEREAL;
        uint32_t const reverse = active_code == SPIRIT_WALKER_CORPOREAL_SPELL ?
            SPIRIT_WALKER_ETHEREAL_SPELL : SPIRIT_WALKER_CORPOREAL_SPELL;
        if (!active_code || !call || !call->item || call->item->code != active_code ||
            !G_TransformUnitType(ent, target)) return false;
        /* The outgoing form's spell cooldown is captured by spell_commit.
         * The reciprocal command must display the same lockout after morph. */
        S_SpellStartCooldownDuration(ent, reverse,
            S_SpellNumber(active_code, ABILITY_NUMBER_COOLDOWN, 1));
        G_SpawnAbilityEffectTarget(active_code, WC3_EFFECT_CASTER, 0, ent, "chest", true);
        G_PlayAbilityEffectSound(active_code, &ent->s.origin2);
        ent->goalentity = NULL;
        if (ent->orders) ent->orders->resource_goal = NULL;
        move_reset_progress(ent);
        unit_setmove(ent, &spirit_walker_morph);
        if (!ent->animation) spirit_walker_morph_finished(ent);
        return true;
    }
    default:
        return CAbilitySimpleSpell(ent, msg, call);
    }
}


/* Ancestral Spirit raises original Tauren and both Spirit Walker forms;
 * resurrection retains the corpse edict and hence its authored form. */
static edict_t *ancestral_spirit_target(edict_t *caster, abilityitem_t const *spell) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    float range = S_SpellRange(spell->code, level), nearest = 0.0f;
    edict_t *selected = NULL;
    FILTER_EDICTS(target, G_UnitIsRaisableCorpse(target) &&
                  (target->class_id == MAKEFOURCC('o','t','a','u') ||
                   target->class_id == MAKEFOURCC('o','s','p','m') ||
                   target->class_id == MAKEFOURCC('o','s','p','w')) && !G_UnitIsHero(target) &&
                  target->s.player == caster->s.player) {
        float distance = Vector2_distance(&target->s.origin2, &caster->s.origin2);
        if ((range <= 0.0f || distance <= range) && (!selected || distance < nearest)) {
            nearest = distance; selected = target;
        }
    }
    return selected;
}

/* Reject an empty cast before the common spell path spends the authored 250 mana. */
static bool ancestral_spirit_validate(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    (void)st;
    return ancestral_spirit_target(caster, spell) != NULL;
}

/* Revive the original edict so JASS handles, owner, unit type and runtime identity remain authoritative. */
static void ancestral_spirit_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    edict_t *target = ancestral_spirit_target(caster, spell);
    uint32_t level = S_SpellLevel(caster, spell->code);
    (void)st;
    if (!target) return;
    G_ReviveCorpse(target, S_SpellData(spell->code, level, 1));
    /* Aast DataB is the percentage of maximum mana restored by revival.
     * Do not change generic G_ReviveCorpse, which other spells also use. */
    target->mana.value = target->mana.max_value * MAX(0.0f, MIN(1.0f,
        S_SpellData(spell->code, level, 2)));
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, target, NULL, true);
}

BZ_VALIDATED_SPELL_PROC(AbilityAncestralSpirit, ancestral_spirit_validate, ancestral_spirit_execute)

/* ---- Purge (Aprg / Apg2 / AIlp) ---------------------------------------------
 * Name=Purge
 * DataA=Movement Update Frequency / ubertip slow factor (fixtures often use a
 *   remaining-speed complement in (0,1]; stock DataA>=1 means factor 1/DataA)
 * DataC=Summoned Unit Damage; DataD=Unit Pause Duration; DataE=Hero Pause Duration
 * BuffID=Bprg (ROC Aprg may omit; fall back to Bprg).
 *
 * After any DataD/DataE pause, reduction lerps from the initial slow to 0 over
 * the remaining buff lifetime.
 */
static heroabilitystatus_t const *purge_status(edict_t const *unit) {
    if (!unit) return NULL;
    FOR_LOOP(i, unit->unit_state ? MAX_UNIT_STATUSES : 0) {
        heroabilitystatus_t const *slot = unit->unit_state->statuses + i;
        abilityitem_t item;
        if (!slot->level || !slot->data) continue;
        if (slot->timestamp && slot->timestamp <= G_Time()) continue;
        item = S_AbilityItem(slot->data);
        if (item.ability && item.ability->proc == CAbilityPurge) return slot;
    }
    return NULL;
}

static float purge_pause_seconds(edict_t const *unit, heroabilitystatus_t const *slot) {
    return S_SpellData(slot->data, slot->level, G_UnitIsHero(unit) ? 5 : 4);
}

static void purge_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    cstring_t buff;
    heroabilitystatus_t *slot;
    bool hostile;
    if (!st.entity) return;
    hostile = S_SpellIsEnemy(caster, st.entity);
    FOR_LOOP(i, st.entity->unit_state ? MAX_UNIT_STATUSES : 0) {
        heroabilitystatus_t *s = st.entity->unit_state->statuses + i;
        if (!s->level || !s->timestamp) continue;
        if (!unit_status_can_magic_dispel(s)) continue;
        unit_removestatus_deferred(st.entity, s, STATUS_REMOVE_DISPEL);
    }
    unit_refreshstatusflags(st.entity);
    G_InvalidateUnitInfoPanel(st.entity);
    /* Purge's cast impact is authored as SpecialArt, not TargetArt. */
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_SPECIAL, 0, st.entity, "origin", true);
    /* The hostile-only movement effect is independent of Purge's dispel. */
    if (hostile) {
        buff = S_SpellBuffId(spell->code, level);
        if (!buff || strlen(buff) < 4) buff = "Bprg";
        slot = S_SpellApplyTimedStatus(st.entity, buff, level, S_SpellDuration(spell->code, level, false));
        if (slot) {
            slot->data = spell->code;
            unit_status_enabletargetart(st.entity, slot, "origin");
        }
    }
    if (st.entity->owner && !S_SummonIsDispelImmune(st.entity))
        S_SpellDamage(st.entity, caster, (int)MAX(1.0f, S_SpellData(spell->code, level, 3)));
    if (hostile && S_PurgeIsImmobilized(st.entity) && st.entity->stand) st.entity->stand(st.entity);
}

BZ_SIMPLE_SPELL_PROC(AbilityPurge) { purge_execute(caster, st, spell); }

/* True while inside DataD (unit) / DataE (hero) pause window of an active Purge. */
bool S_PurgeIsImmobilized(edict_t const *unit) {
    heroabilitystatus_t const *slot = purge_status(unit);
    float pause;
    uint32_t start, pause_ms;
    if (!slot || !slot->duration_ms || slot->timestamp < slot->duration_ms) return false;
    pause = purge_pause_seconds(unit, slot);
    if (pause <= 0.0f) return false;
    start = slot->timestamp - slot->duration_ms;
    pause_ms = (uint32_t)(pause * 1000.0f);
    return G_Time() < start + pause_ms;
}

/* Pause is full stop; afterward initial DataA slow recovers linearly over Dur-pause. */
float S_PurgeMoveReduction(edict_t const *unit) {
    heroabilitystatus_t const *slot = purge_status(unit);
    float dataA, initial, pause, progress;
    uint32_t start, pause_ms, slow_ms, elapsed;
    if (!slot) return 0.0f;
    if (S_PurgeIsImmobilized(unit)) return 1.0f;
    dataA = S_SpellData(slot->data, slot->level, 1);
    if (dataA <= 0.0f) initial = 1.0f;
    else if (dataA > 1.0f) initial = 1.0f - (1.0f / dataA); /* stock factor */
    else initial = 1.0f - dataA; /* fixture remaining-speed complement */
    if (initial <= 0.0f) return 0.0f;
    if (!slot->duration_ms || slot->timestamp < slot->duration_ms) return initial;
    start = slot->timestamp - slot->duration_ms;
    pause = purge_pause_seconds(unit, slot);
    pause_ms = (uint32_t)(MAX(0.0f, pause) * 1000.0f);
    if (slot->duration_ms <= pause_ms) return initial;
    slow_ms = slot->duration_ms - pause_ms;
    if (G_Time() <= start + pause_ms) return initial;
    elapsed = G_Time() - (start + pause_ms);
    progress = MIN(1.0f, (float)elapsed / (float)slow_ms);
    return initial * (1.0f - progress);
}

/* ---- Lightning Shield (Alsh) -----------------------------------------------
 * Name=Lightning Shield
 * Ubertip="Forms a shield of electricity around a target unit, dealing
 *          <Alsh,DataA1> damage per second to units around it. |nLasts
 *          <Alsh,Dur1> seconds."
 *
 * Applies the Blsh buff to the target and spawns a periodic thinker that
 * damages all other living units within Area of the carrier each second.
 * Attribution uses the original caster for damage and resistance calculations.
 */
void lsh_think(edict_t *thinker) {
    edict_t *carrier = S_SpellChannelOwner(thinker);
    edict_t *caster = S_SpellChannelTarget(thinker);
    uint32_t level;
    float area, damage;
    if (!carrier || !caster) { G_FreeEdict(thinker); return; }
    level = G_UnitStatusLevel(carrier, MAKEFOURCC('B', 'l', 's', 'h'));
    if (!level || G_Time() >= thinker->spawn_time) { G_FreeEdict(thinker); return; }
    if (thinker->freetime && G_Time() < thinker->freetime) return;
    area = S_SpellNumber(MAKEFOURCC('A', 'l', 's', 'h'), ABILITY_NUMBER_AREA, level);
    damage = S_SpellData(MAKEFOURCC('A', 'l', 's', 'h'), level, 1);
    FILTER_EDICTS(target, target != carrier && S_SpellIsAliveTarget(target) &&
                  Vector2_distance(&target->s.origin2, &carrier->s.origin2) <= area)
        S_SpellDamage(target, caster, (int)MAX(1.0f, damage));
    thinker->freetime = G_Time() + 1000;
}

/* Lightning Shield owns a periodic thinker. Reject capacity failures before
 * committing the cast and never spawn that thinker without its carrier buff. */
static cstring_t lightning_shield_buff(abilityitem_t const *spell, uint32_t level) {
    cstring_t buff = spell ? S_SpellBuffId(spell->code, level) : NULL;
    if (buff) return buff;
#if BZ_COMPAT_WORKAROUNDS
    /* RoC 1.0 omits Alsh's BuffID; the corresponding status is Blsh. */
    if (spell && (G_AbilityCode(spell->code) == MAKEFOURCC('A','l','s','h') ||
                  G_AbilityCode(spell->code) == MAKEFOURCC('A','C','l','s'))) return "Blsh";
#endif
    return NULL;
}

static bool lightning_shield_validate(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t level;
    cstring_t buff;
    wc3_status_apply_check_t check;
    if (!caster || !spell || !st.entity || !S_SpellIsAliveTarget(st.entity)) return false;
    level = S_SpellLevel(caster, spell->code);
    buff = lightning_shield_buff(spell, level);
    if (!buff || strlen(buff) < 4) return false;
    check = unit_status_checkapplication(st.entity, &(status_application_t){
        .buff = buff, .level = level
    });
    return check == WC3_STATUS_APPLY_REUSE || check == WC3_STATUS_APPLY_FREE_SLOT;
}

static void lightning_shield_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    float dur = S_SpellDuration(spell->code, level, false);
    cstring_t buff = lightning_shield_buff(spell, level);
    edict_t *thinker;
    if (!st.entity || !buff || strlen(buff) < 4) return;
    if (!S_SpellApplyTimedStatus(st.entity, buff, level, dur)) return;
    unit_status_enabletargetart(st.entity, unit_findstatus(st.entity, FS_SLKKey(buff)), "origin");
    thinker = S_SpellIdentityThinker(st.entity, spell->code, caster);
    if (!thinker) return;
    thinker->spawn_time = G_Time() + (uint32_t)(dur * 1000.0f);
    thinker->think = lsh_think; lsh_think(thinker);
}

BZ_VALIDATED_SPELL_PROC(AbilityLightningShield, lightning_shield_validate, lightning_shield_execute)

/* ---- Healing Ward (Ahwd) ---------------------------------------------------
 * Name=Healing Ward
 * Ubertip="Summons an immovable ward that heals <Aoar,DataA1,%>% of a nearby
 *          friendly non-mechanical unit's hit points per second. |nLasts
 *          <Ahwd,Dur1> seconds."
 *
 * Spawns the ward unit at the target point; the ward's Aoar ability drives the
 * S_RegenerationHealthAura path in s_hero_passives.c.
 */
BZ_SIMPLE_SPELL_PROC(AbilityHealingWard) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    S_SummonAbilityAt(caster, spell->code, S_SpellUnitId(spell->code, level), &st.point,
                      S_SpellDuration(spell->code, level, false));
}

/* Passive marker for the regen-life aura families (Aoar/Aabr).
 * The per-second HP regeneration is computed by S_RegenerationHealthAura; this
 * procedure exists only to provide a named TFT-class entry in the registry. */
BZ_ABILITY_PROC(CAbilityAuraRegenLife) { return CAbilityPassive(ent, msg, call); }
