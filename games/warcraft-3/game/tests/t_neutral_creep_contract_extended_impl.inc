/* Shared gameplay test runners for the abilities not covered by the original
 * neutral creep suite. The tests intentionally FAIL on absent effect handlers.
 * These are synthetic ability fixtures; retail alias ownership is verified
 * independently by the matrix, never inferred from the English name. */
typedef enum {
    CE_AURA_TRUESHOT, CE_AURA_COMMAND, CE_AURA_UNHOLY,
    CE_AURA_ENDURANCE, CE_AURA_THORNS, CE_AURA_DEVOTION,
    CE_AURA_BRILLIANCE, CE_AURA_VAMPIRIC,
    CE_UNIT_SLEEP, CE_UNIT_DOT_AOE, CE_UNIT_DEVOUR, CE_UNIT_PURGE,
    CE_UNIT_CHAIN, CE_UNIT_MANA_BURN, CE_UNIT_STUN_DAMAGE,
    CE_UNIT_CYCLONE, CE_UNIT_FRIEND_BUFF, CE_UNIT_FRIEND_ARMOR,
    CE_UNIT_POLYMORPH, CE_UNIT_FROST_NOVA, CE_UNIT_ROOT,
    CE_UNIT_CRIPPLE, CE_UNIT_DOT, CE_UNIT_WEB, CE_UNIT_HEAL_CHAIN,
    CE_UNIT_FRIEND_FRENZY, CE_UNIT_POSSESSION, CE_UNIT_LIFE_DRAIN,
    CE_UNIT_DEATH_COIL, CE_UNIT_CHARM, CE_UNIT_DAMAGE,
    CE_SELF_DAMAGE_STUN, CE_SELF_IMMOLATION, CE_SELF_INVULNERABLE,
    CE_SELF_MANA_SHIELD, CE_SELF_ENEMY_DEBUFF,
    CE_POINT_DAMAGE, CE_POINT_DISPEL, CE_POINT_DISPEL_DRAIN,
    CE_POINT_SILENCE, CE_POINT_WARD,
    CE_SUMMON_NO_TARGET, CE_SUMMON_POINT, CE_SUMMON_CORPSE,
    CE_SUMMON_CORPSE_HEAL,
    CE_HIT_POISON, CE_HIT_SLOW_POISON, CE_HIT_CRITICAL,
    CE_HIT_LIGHTNING, CE_HIT_SEARING, CE_HIT_BLACK_ARROW,
    CE_HIT_COLD_ARROW, CE_HIT_SPLASH, CE_HIT_REFLECT,
    CE_PASSIVE_REINCARNATION, CE_PASSIVE_DISEASE, CE_PASSIVE_RESISTANT,
    CE_SHADOW_MELD, CE_HIDE
} creepExpectation_t;

typedef struct { int row; cstring_t creep, ability, description, url;
                 cstring_t base_code, verified_alias; creepExpectation_t expectation; } creepExtendedCase_t;

/* The regular fixture already injects AbilityData and four factions/targets.
 * This extended row adds projectile, buff, UnitID and multi-stage spell data,
 * with non-retail values so the code must read the SLK columns. */
static void contract_extended_slk(char *out, size_t len, cstring_t alias,
                                  cstring_t parent, cstring_t buff, cstring_t targets,
                                  creepExpectation_t kind) {
    float a = 37.0f, b = 2.0f, c = 1.0f, d = 1.0f, e = 0.25f;
    char data_a[24], data_b[24], data_c[24];
    switch (kind) {
    case CE_AURA_TRUESHOT: case CE_AURA_COMMAND: case CE_AURA_VAMPIRIC:
    case CE_AURA_THORNS: a = 0.20f; break;
    case CE_AURA_UNHOLY: case CE_AURA_ENDURANCE: a = 0.10f; b = 0.5f; break;
    case CE_AURA_DEVOTION: a = 3.0f; break;
    case CE_AURA_BRILLIANCE: a = 1.0f; break;
    case CE_HIT_CRITICAL: a = 100.0f; b = 2.0f; break;
    case CE_HIT_SPLASH: a = 100.0f; b = 23.0f; c = 64.0f; d = 96.0f; break;
    case CE_HIT_REFLECT: a = 0.30f; b = 3.0f; break;
    case CE_HIT_POISON: a = 2.0f; break;
    case CE_HIT_SLOW_POISON: a = 5.0f; b = 0.25f; c = 0.15f; break;
    case CE_SHADOW_MELD: case CE_HIDE: a = 1.5f; break;
    case CE_PASSIVE_REINCARNATION: a = 1.0f; break;
    case CE_SELF_IMMOLATION: a = 11.0f; b = 7.0f; break;
    case CE_UNIT_MANA_BURN: a = 37.0f; break;
    case CE_SUMMON_NO_TARGET: a = 2.0f; break;
    case CE_SUMMON_POINT: a = 3.0f; break;
    case CE_SUMMON_CORPSE: a = 2.0f; b = 0.0f; break;
    default: break;
    }
    snprintf(data_a, sizeof(data_a), "%g", a);
    if (kind == CE_UNIT_POLYMORPH) strlcpy(data_a, "hfoo", sizeof(data_a));
    snprintf(data_b, sizeof(data_b), "%g", b);
    if (kind == CE_UNIT_POLYMORPH) strlcpy(data_b, "hfoo", sizeof(data_b));
    snprintf(data_c, sizeof(data_c), "%g", c);
    if (kind == CE_SUMMON_CORPSE && strcmp(alias, "Arai") == 0)
        strcpy(data_c, "hfoo"); /* fixture skeleton, not retail uske */
    snprintf(out, len,
        "ID;PWXL;N;EBB;Y2;X20\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n"
        "C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Dur1\"\nC;Y1;X9;K\"HeroDur1\"\n"
        "C;Y1;X10;K\"DataA1\"\nC;Y1;X11;K\"DataB1\"\nC;Y1;X12;K\"DataC1\"\n"
        "C;Y1;X13;K\"DataD1\"\nC;Y1;X14;K\"DataE1\"\nC;Y1;X15;K\"UnitID1\"\n"
        "C;Y1;X16;K\"BuffID1\"\nC;Y1;X17;K\"Area1\"\n"
        "C;Y1;X18;K\"Cast1\"\nC;Y1;X19;K\"Missileart\"\nC;Y1;X20;K\"Ubertip\"\n"
        "C;Y2;X1;K\"%s\"\nC;Y2;X2;K\"%s\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"%s\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"0\"\n"
        "C;Y2;X7;K\"600\"\nC;Y2;X8;K\"6\"\nC;Y2;X9;K\"3\"\n"
        "C;Y2;X10;K\"%s\"\nC;Y2;X11;K\"%s\"\nC;Y2;X12;K\"%s\"\n"
        "C;Y2;X13;K\"%g\"\nC;Y2;X14;K\"%g\"\n"
        "C;Y2;X15;K\"hfoo\"\nC;Y2;X16;K\"%s\"\n"
        "C;Y2;X17;K\"240\"\nC;Y2;X18;K\"0\"\n"
        "C;Y2;X19;K\"\"\nC;Y2;X20;K\"creep test fixture\"\nE\n",
        alias, parent, targets, data_a, data_b, data_c, d, e, buff ? buff : "");
}

static uint32_t contract_ext_count_summons(edict_t const *caster) {
    uint32_t n = 0;
    FILTER_EDICTS(ent, ent->inuse && ent->owner == caster &&
                  ent->class_id == FS_SLKKey("hfoo") && ent != caster) n++;
    return n;
}

static void contract_ext_tick(uint32_t millis, edict_t *unit) {
    (void)unit; /* G_RunEntities owns ability updates and status scheduling. */
    contract_run_for(millis);
}

static cstring_t contract_ext_buff(creepExpectation_t expectation) {
    switch (expectation) {
    /* AUsl/ACsl sleep is the dispellable spell status BUsl. Bslp is the
     * separate natural creep-sleep status and does not describe this cast. */
    case CE_UNIT_SLEEP: return "BUsl";
    case CE_UNIT_CYCLONE: return "Bcyc";
    case CE_UNIT_ROOT: return "BEer";
    case CE_UNIT_CRIPPLE: return "Bcri";
    case CE_UNIT_PURGE: return "Bprg";
    case CE_UNIT_WEB: return "Bwea";
    case CE_UNIT_FRIEND_BUFF: return "Binf";
    case CE_UNIT_FRIEND_ARMOR: return "BUfa";
    case CE_UNIT_FRIEND_FRENZY: return "BUhf";
    case CE_UNIT_POLYMORPH: return "Bply";
    case CE_UNIT_DOT: return "Bpoi";
    case CE_UNIT_DOT_AOE: return "Blsh";
    case CE_UNIT_FROST_NOVA: return "Bfro";
    case CE_UNIT_STUN_DAMAGE: case CE_SELF_DAMAGE_STUN: return "Bstu";
    case CE_POINT_SILENCE: return "BNsi";
    case CE_HIT_POISON: return "Bpoi";
    case CE_HIT_SLOW_POISON: return "Bspo";
    case CE_PASSIVE_DISEASE: return "Bapl";
    default: return "Bcre"; /* intentionally synthetic, not asserted as a retail buff */
    }
}

static float contract_ext_aura(creepExpectation_t expect, edict_t *recipient) {
    switch (expect) {
    case CE_AURA_TRUESHOT: return S_TrueshotAttackBonus(recipient);
    case CE_AURA_COMMAND: return S_CommandAuraAttackBonus(recipient);
    case CE_AURA_UNHOLY: return S_UnholyMoveBonus(recipient) + S_UnholyHealthRegen(recipient);
    case CE_AURA_ENDURANCE: return S_EnduranceMoveBonus(recipient) + S_EnduranceAttackBonus(recipient);
    case CE_AURA_THORNS: return S_ThornsMeleeDamageReturn(recipient, 20);
    case CE_AURA_DEVOTION: return S_DevotionArmorBonus(recipient);
    case CE_AURA_BRILLIANCE: return S_BrillianceManaRegen(recipient);
    case CE_AURA_VAMPIRIC: return S_VampiricLifeSteal(recipient);
    default: return 0;
    }
}

/* Forward-declared: cases follow the runner and capture source row/name/URL. */
static void contract_extended(creepExtendedCase_t const *c);

/* Strict behavior tests. A successful spell dispatch is NOT considered proof:
 * tests inspect the receiver's externally observable state, time progression,
 * ownership or a second victim. Unsupported game behavior remains a red test. */
static void contract_extended(creepExtendedCase_t const *c) {
    creepContractFix_t f;
    char slk[4096];
    slkTestData_t *old_ui = NULL, *ui_rows = NULL;
    cstring_t code = c->verified_alias ? c->verified_alias : c->base_code;
    creepExpectation_t k = c->expectation;
    uint32_t id = FS_SLKKey(code), buff = FS_SLKKey(contract_ext_buff(k));
    vec2_t point = { 100.0f, 0.0f };
    float before;
    float old_daytime = G_GetTimeOfDay();

    /* Existing registry parent code is a *fixture selection*. The attached
     * verified alias (if any) remains a separate, explicit contract. */
    contract_extended_slk(slk, sizeof(slk), code, c->base_code,
                          contract_ext_buff(k),
                          (k == CE_AURA_TRUESHOT || k == CE_AURA_COMMAND ||
                           k == CE_AURA_UNHOLY || k == CE_AURA_ENDURANCE ||
                           k == CE_AURA_THORNS || k == CE_AURA_DEVOTION ||
                           k == CE_AURA_BRILLIANCE || k == CE_AURA_VAMPIRIC ||
                           k == CE_UNIT_FRIEND_BUFF || k == CE_UNIT_FRIEND_ARMOR ||
                           k == CE_UNIT_FRIEND_FRENZY || k == CE_UNIT_DOT_AOE ||
                           k == CE_UNIT_HEAL_CHAIN) ? "air,ground,friend,organic" :
                          "air,ground,enemy,neutral,organic", k);
    if (k == CE_UNIT_POLYMORPH) {
        static char const unit_ui[] =
            "ID;PWXL;N;EBB;Y2;X4\n"
            "C;Y1;X1;K\"unitUI\"\nC;Y1;X2;K\"file\"\n"
            "C;Y2;X1;K\"hfoo\"\n"
            "C;Y2;X2;K\"Units\\\\Human\\\\Footman\\\\Footman\"\nE\n";
        ui_rows = parse_slk_string(unit_ui);
        old_ui = G_SetSLKRows("UnitUI", ui_rows);
    }
    contract_setup(&f, code, slk);
    f.caster->unit_state->ranks[0] = MAKE(heroability_t, .code = id, .level = 1);
    T_ASSERT(G_ActorHasSkill(f.caster, code));
    T_EQ(S_ResolveAbilityAlias(f.caster, FS_SLKKey(c->base_code)).alias, id);
    if (k == CE_AURA_TRUESHOT) {
        f.ally->unit_state->attack1.type = ATK_NORMAL;
        f.ally->unit_state->attack1.weapon = WPN_MISSILE;
    }
    /* An out-of-range hostile control distinguishes area effects from accidental
     * global damage. A second nearby hostile verifies bounce/splash. */
    edict_t *far = alloc_test_unit(FS_SLKKey("ogru"), 900, 0);
    far->svflags |= SVF_MONSTER; far->targtype = TARG_GROUND;
    far->s.player = 1; far->health.value = far->health.max_value = 500;

    switch (k) {
    case CE_AURA_TRUESHOT: case CE_AURA_COMMAND: case CE_AURA_UNHOLY:
    case CE_AURA_ENDURANCE: case CE_AURA_THORNS: case CE_AURA_DEVOTION:
    case CE_AURA_BRILLIANCE: case CE_AURA_VAMPIRIC:
        S_UpdateHeroAuraEffects(f.ally);
        T_ASSERT(contract_ext_aura(k, f.ally) > 0.0f);
        before = contract_ext_aura(k, f.ally);
        f.ally->s.origin2.x = 900;
        contract_ext_tick(AURA_UPDATE_MS, f.ally);
        T_ASSERT(contract_ext_aura(k, f.ally) < before);
        break;

    case CE_UNIT_STUN_DAMAGE:
        T_ASSERT(S_CastUnitTargetSpell(f.caster, id, f.enemy));
        T_ASSERT(f.enemy->health.value < 500);
        T_ASSERT(S_UnitHasStatus(f.enemy, buff) ||
                 unit_hasstatusstate(f.enemy, WC3_STATUS_STATE_STUNNED));
        T_FEQ(f.ally->health.value, 100, 0.001f);
        break;
    case CE_UNIT_MANA_BURN:
        before = f.enemy->mana.value;
        T_ASSERT(S_CastUnitTargetSpell(f.caster, id, f.enemy));
        T_ASSERT(f.enemy->mana.value < before);
        T_ASSERT(f.enemy->health.value < 500);
        T_FEQ(f.ally->mana.value, 500, 0.001f);
        break;
    case CE_UNIT_FROST_NOVA:
        T_ASSERT(S_CastUnitTargetSpell(f.caster, id, f.enemy));
        T_ASSERT(f.enemy->health.value < 500);
        T_ASSERT(f.summon->health.value < 500);
        T_ASSERT(S_UnitHasStatus(f.enemy, buff));
        T_FEQ(far->health.value, 500, 0.001f);
        break;
    case CE_UNIT_DOT_AOE:
        T_ASSERT(S_CastUnitTargetSpell(f.caster, id, f.ally));
        T_ASSERT(S_UnitHasStatus(f.ally, buff));
        contract_ext_tick(1000, f.caster);
        T_ASSERT(f.enemy->health.value < 500);
        break;
    case CE_UNIT_CHAIN:
        T_ASSERT(S_CastUnitTargetSpell(f.caster, id, f.enemy));
        T_ASSERT(f.enemy->health.value < 500);
        T_ASSERT(f.summon->health.value < 500);
        T_FEQ(far->health.value, 500, 0.001f);
        break;
    case CE_UNIT_DAMAGE:
        T_ASSERT(S_CastUnitTargetSpell(f.caster, id, f.enemy));
        T_ASSERT(f.enemy->health.value < 500);
        T_FEQ(f.ally->health.value, 100, 0.001f);
        break;
    case CE_UNIT_SLEEP:
        T_ASSERT(S_CastUnitTargetSpell(f.caster, id, f.enemy));
        T_ASSERT(S_UnitHasStatus(f.enemy, buff));
        S_ResolveAttackHit(f.caster, f.enemy, 10);
        T_ASSERT(!S_UnitHasStatus(f.enemy, buff));
        break;
    case CE_UNIT_CYCLONE:
        T_ASSERT(S_CastUnitTargetSpell(f.caster, id, f.enemy));
        T_ASSERT(S_UnitIsCycloned(f.enemy));
        contract_ext_tick(6000, f.enemy);
        T_ASSERT(!S_UnitIsCycloned(f.enemy));
        break;
    case CE_UNIT_ROOT:
        T_ASSERT(S_CastUnitTargetSpell(f.caster, id, f.enemy));
        T_ASSERT(S_UnitIsEntanglingRooted(f.enemy));
        contract_ext_tick(1000, f.enemy);
        T_ASSERT(f.enemy->health.value < 500);
        break;
    case CE_UNIT_CRIPPLE:
        T_ASSERT(S_CastUnitTargetSpell(f.caster, id, f.enemy));
        T_ASSERT(S_CrippleMoveReduction(f.enemy) > 0.0f);
        T_ASSERT(S_CrippleAttackReduction(f.enemy) > 0.0f);
        T_ASSERT(S_CrippleDamageReduction(f.enemy) > 0.0f);
        break;
    case CE_UNIT_POLYMORPH:
        T_ASSERT(S_CastUnitTargetSpell(f.caster, id, f.enemy));
        T_ASSERT(S_UnitPolymorphed(f.enemy));
        contract_ext_tick(6000, f.enemy);
        T_ASSERT(!S_UnitPolymorphed(f.enemy));
        break;
    case CE_UNIT_WEB:
        f.enemy->targtype = TARG_AIR;
        f.enemy->s.origin.z = 128.0f;
        T_ASSERT(S_CastUnitTargetSpell(f.caster, id, f.enemy));
        T_ASSERT(S_UnitIsEnsnared(f.enemy));
        break;
    case CE_UNIT_DOT:
        T_ASSERT(S_CastUnitTargetSpell(f.caster, id, f.enemy));
        T_ASSERT(S_UnitHasStatus(f.enemy, buff));
        before = f.enemy->health.value;
        contract_ext_tick(1000, f.enemy);
        T_ASSERT(f.enemy->health.value < before);
        break;
    case CE_UNIT_DEVOUR:
        T_ASSERT(S_CastUnitTargetSpell(f.caster, id, f.enemy));
        T_ASSERT(S_DevourHasVictim(f.caster));
        T_ASSERT((f.enemy->aiflags & AI_DEVOURED) != 0);
        T_ASSERT((f.enemy->s.renderfx & RF_HIDDEN) != 0);
        contract_ext_tick(1000, f.enemy);
        T_ASSERT(f.enemy->health.value < 500);
        T_FEQ(f.ally->health.value, 100, 0.001f);
        break;
    case CE_UNIT_PURGE:
        T_ASSERT(unit_addtimedstatus(f.enemy, "Binf", 1, 30));
        heroabilitystatus_t *purge_status = unit_findstatus(f.enemy, FS_SLKKey("Binf"));
        T_NOT_NULL(purge_status);
        if (purge_status) purge_status->buff_flags |= WC3_STATUS_BUFF_MAGICAL | WC3_STATUS_BUFF_POSITIVE;
        T_ASSERT(S_CastUnitTargetSpell(f.caster, id, f.enemy));
        T_ASSERT(!S_UnitHasStatus(f.enemy, FS_SLKKey("Binf")));
        T_ASSERT(S_PurgeMoveReduction(f.enemy) > 0.0f);
        break;
    case CE_UNIT_FRIEND_BUFF: case CE_UNIT_FRIEND_ARMOR:
    case CE_UNIT_FRIEND_FRENZY:
        before = G_UnitArmorValue(f.ally);
        T_ASSERT(S_CastUnitTargetSpell(f.caster, id, f.ally));
        T_ASSERT(S_UnitHasStatus(f.ally, buff));
        if (k == CE_UNIT_FRIEND_ARMOR || k == CE_UNIT_FRIEND_BUFF)
            T_ASSERT(G_UnitArmorValue(f.ally) > before);
        if (k == CE_UNIT_FRIEND_FRENZY) {
            float hp = f.ally->health.value;
            contract_ext_tick(1000, f.ally);
            T_ASSERT(f.ally->health.value < hp);
        }
        break;
    case CE_UNIT_HEAL_CHAIN:
        f.caster->health.value = 200; /* Another wounded nearby ally: bounce */
        T_ASSERT(S_CastUnitTargetSpell(f.caster, id, f.ally));
        T_ASSERT(f.ally->health.value > 100);
        T_ASSERT(f.caster->health.value > 200);
        break;
    case CE_UNIT_LIFE_DRAIN:
        f.caster->health.value = 200;
        T_ASSERT(S_CastUnitTargetSpell(f.caster, id, f.enemy));
        contract_ext_tick(1000, f.caster);
        T_ASSERT(f.enemy->health.value < 500);
        T_ASSERT(f.caster->health.value > 200);
        break;
    case CE_UNIT_DEATH_COIL:
        T_ASSERT(S_CastUnitTargetSpell(f.caster, id, f.enemy));
        T_ASSERT(f.enemy->health.value < 500);
        break;
    case CE_UNIT_POSSESSION: case CE_UNIT_CHARM:
        T_ASSERT(S_CastUnitTargetSpell(f.caster, id, f.enemy));
        T_EQ(f.enemy->s.player, f.caster->s.player);
        break;

    case CE_POINT_DAMAGE:
        T_ASSERT(S_CastPointTargetSpell(f.caster, id, &point));
        contract_ext_tick(3000, f.caster);
        T_ASSERT(f.enemy->health.value < 500);
        T_FEQ(far->health.value, 500, 0.001f);
        T_FEQ(f.ally->health.value, 100, 0.001f);
        break;
    case CE_POINT_SILENCE:
        T_ASSERT(S_CastPointTargetSpell(f.caster, id, &point));
        T_ASSERT(S_UnitIsSilenced(f.enemy));
        T_ASSERT(!S_UnitIsSilenced(f.ally));
        T_ASSERT(!S_UnitIsSilenced(far));
        contract_ext_tick(6000, f.enemy);
        T_ASSERT(!S_UnitIsSilenced(f.enemy));
        break;
    case CE_POINT_DISPEL: case CE_POINT_DISPEL_DRAIN:
        T_ASSERT(unit_addtimedstatus(f.enemy, "Binf", 1, 30));
        heroabilitystatus_t *dispel_status = unit_findstatus(f.enemy, FS_SLKKey("Binf"));
        T_NOT_NULL(dispel_status);
        if (dispel_status) dispel_status->buff_flags |= WC3_STATUS_BUFF_MAGICAL | WC3_STATUS_BUFF_POSITIVE;
        if (k == CE_POINT_DISPEL_DRAIN) {
            f.caster->health.value = 200;
            f.caster->mana.value = 200;
        }
        T_ASSERT(S_CastPointTargetSpell(f.caster, id, &point));
        T_ASSERT(!S_UnitHasStatus(f.enemy, FS_SLKKey("Binf")));
        if (k == CE_POINT_DISPEL_DRAIN)
            T_ASSERT(f.caster->health.value > 200 || f.caster->mana.value > 200);
        break;
    case CE_POINT_WARD:
        T_ASSERT(S_CastPointTargetSpell(f.caster, id, &point));
        T_ASSERT(contract_ext_count_summons(f.caster) >= 1);
        break;
    case CE_SELF_DAMAGE_STUN:
        T_ASSERT(S_CastNoTargetSpell(f.caster, id));
        T_ASSERT(f.enemy->health.value < 500);
        T_ASSERT(f.summon->health.value < 500);
        T_ASSERT(unit_hasstatusstate(f.enemy, WC3_STATUS_STATE_STUNNED) ||
                 S_UnitHasStatus(f.enemy, FS_SLKKey("Bstu")));
        T_FEQ(far->health.value, 500, 0.001f);
        break;
    case CE_SELF_IMMOLATION:
        /* Permanent Immolation is a passive ambient effect.  It must not
         * require an explicit cast; ordinary Immolation must activate first. */
        if (strcmp(c->ability, "Permanent Immolation") != 0)
            T_ASSERT(S_CastNoTargetSpell(f.caster, id));
        contract_ext_tick(1000, f.caster);
        T_ASSERT(f.enemy->health.value < 500);
        T_FEQ(far->health.value, 500, 0.001f);
        break;
    case CE_SELF_INVULNERABLE:
        T_ASSERT(S_CastNoTargetSpell(f.caster, id));
        T_ASSERT(G_UnitHasFlag(f.caster, UNIT_FLAG_INVULNERABLE) ||
                 unit_hasstatusstate(f.caster, WC3_STATUS_STATE_INVULNERABLE));
        before = f.caster->health.value;
        S_ResolveAttackHit(f.enemy, f.caster, 30);
        T_FEQ(f.caster->health.value, before, 0.001f);
        break;
    case CE_SELF_MANA_SHIELD:
        T_ASSERT(S_CastNoTargetSpell(f.caster, id));
        before = f.caster->mana.value;
        T_ASSERT(S_ManaShieldDamage(f.caster, 40) < 40);
        T_ASSERT(f.caster->mana.value < before);
        break;
    case CE_SELF_ENEMY_DEBUFF:
        T_ASSERT(S_CastNoTargetSpell(f.caster, id));
        T_ASSERT(S_UnitHasStatus(f.enemy, buff));
        T_ASSERT(!S_UnitHasStatus(f.ally, buff));
        break;

    case CE_SUMMON_NO_TARGET:
        T_ASSERT(S_CastNoTargetSpell(f.caster, id));
        T_ASSERT(contract_ext_count_summons(f.caster) >=
                 (strcmp(c->ability, "Feral Spirit") == 0 ||
                  strcmp(c->ability, "Summon Prawns") == 0 ? 2u : 1u));
        break;
    case CE_SUMMON_CORPSE: case CE_SUMMON_CORPSE_HEAL: {
        UnitData_t corpse_data = { .deathType = 3 };
        f.enemy->data.UnitData = &corpse_data;
        f.caster->runtime.acquisition_range = 600.0f;
        f.enemy->die = unit_die;
        unit_die(f.enemy, f.caster);
        T_ASSERT(M_IsDead(f.enemy));
        if (k == CE_SUMMON_CORPSE) {
            T_ASSERT(S_CastNoTargetSpell(f.caster, id));
            if (strcmp(c->ability, "Raise Dead (Autocast)") == 0)
                T_ASSERT(contract_ext_count_summons(f.caster) >= 2);
            else
                T_ASSERT(!M_IsDead(f.enemy) && f.enemy->owner == f.caster);
        } else {
            f.caster->health.value = 200;
            T_ASSERT(S_CastNoTargetSpell(f.caster, id));
            contract_ext_tick(1000, f.caster);
            T_ASSERT(f.caster->health.value > 200);
        }
        break;
    }
    case CE_SUMMON_POINT:
        T_ASSERT(S_CastPointTargetSpell(f.caster, id, &point));
        T_ASSERT(contract_ext_count_summons(f.caster) >= 1);
        break;

    case CE_HIT_POISON: case CE_HIT_SLOW_POISON:
        if (k == CE_HIT_POISON) S_PoisonOnHit(f.caster, f.enemy);
        else S_SlowPoisonOnHit(f.caster, f.enemy);
        T_ASSERT(S_UnitHasStatus(f.enemy, buff));
        T_ASSERT(!S_UnitHasStatus(f.ally, buff));
        break;
    case CE_HIT_CRITICAL:
        T_ASSERT(S_CriticalStrikeDamage(f.caster, 20) > 20);
        break;
    case CE_HIT_LIGHTNING: case CE_HIT_SEARING: case CE_HIT_BLACK_ARROW:
    case CE_HIT_COLD_ARROW:
        if (k == CE_HIT_SEARING || k == CE_HIT_COLD_ARROW || k == CE_HIT_BLACK_ARROW) {
            f.caster->unit_state->attack1.weapon = WPN_MISSILE;
            T_ASSERT(G_SetUnitAutocast(f.caster, id, true));
        }
        /* Compare the exact same primary hit against an unskilled baseline:
         * extra damage, resource expenditure or a cold status must differ. */
        before = f.enemy->health.value;
        S_ResolveAttackHit(f.caster, f.enemy, 20);
        T_ASSERT(f.enemy->health.value < before - 20 ||
                 f.caster->mana.value < 500 ||
                 (k == CE_HIT_COLD_ARROW && S_UnitHasStatus(f.enemy, buff)));
        break;
    case CE_HIT_SPLASH:
        S_PulverizeAttack(f.caster, f.enemy);
        T_ASSERT(f.summon->health.value < 500);
        T_FEQ(f.ally->health.value, 100, 0.001f);
        break;
    case CE_HIT_REFLECT:
        T_ASSERT(S_SpikedDamageReturn(f.caster, 20) > 0.0f);
        S_ResolveAttackHit(f.enemy, f.caster, 20);
        T_ASSERT(f.enemy->health.value < 500);
        break;
    case CE_PASSIVE_REINCARNATION:
        f.caster->health.value = 0;
        S_ReincarnationOnDeath(f.caster);
        contract_ext_tick(6000, f.caster);
        T_ASSERT(f.caster->inuse && f.caster->health.value > 0);
        break;
    case CE_PASSIVE_RESISTANT:
        T_ASSERT(S_UnitIsResistant(f.caster));
        break;
    case CE_PASSIVE_DISEASE:
        contract_ext_tick(2000, f.caster);
        T_ASSERT(S_UnitHasStatus(f.enemy, buff));
        break;
    case CE_SHADOW_MELD: case CE_HIDE:
        G_SetTimeOfDay(game.constants.duskTimeGameHours);
        G_UpdateTimeOfDay();
        if (k == CE_HIDE) T_ASSERT(unit_issueimmediateorder(f.caster, "ambush"));
        contract_ext_tick(1600, f.caster);
        T_ASSERT(S_UnitIsInvisibleToPlayer(f.caster, 1));
        break;
    }
    G_SetTimeOfDay(old_daytime); G_UpdateTimeOfDay();
    if (old_ui) {
        G_SetSLKRows("UnitUI", old_ui);
        free_slk_rows(ui_rows);
    }
    contract_done(&f);
}
