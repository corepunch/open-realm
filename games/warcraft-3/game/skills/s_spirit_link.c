#include "s_skills.h"

#define BZ_BSPL MAKEFOURCC('B', 's', 'p', 'l') // rawcode; Spirit Link buff
#define SPL_MAX_CANDS 64 // units; static scratch capacity for DataB nearest selection

/* Bounded nearest-N insertion over the full eligible population. Only
 * strictly-closer candidates displace the retained farthest, so equal
 * distances keep edict order (deterministic tie-break). */
static uint32_t spirit_link_nearest_insert(edict_t * *cands, float *dists, uint32_t n, uint32_t maxn, edict_t *target, float dist) {
	uint32_t far = 0;
	FOR_LOOP(i, n) if (dists[i] > dists[far]) far = i;
	if (n < maxn) { cands[n] = target; dists[n] = dist; return n + 1; }
	if (dist < dists[far]) { cands[far] = target; dists[far] = dist; }
	return n;
}

static void spirit_link_store_code(edict_t *unit, uint32_t buff, uint32_t code) {
	FOR_LOOP(i, MAX_UNIT_STATUSES)
		if (unit->abilstatus[i].level && unit->abilstatus[i].code == buff) {
			unit->abilstatus[i].data = code; break;
		}
}

/* Apply Bspl to up to DataB nearest valid units in Area of the click target.
 * The click target is kept first when valid; remaining slots go to the
 * nearest others. Equal distances keep edict order. */
static void spirit_link_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
	uint32_t level, buff, maxn, n = 0, j, rest, restmax;
	bool clicked;
	float area, dur, dist;
	cstring_t buffstr;
	edict_t *cands[SPL_MAX_CANDS];
	float dists[SPL_MAX_CANDS];
	if (!st.entity || !spell) return;
	level = S_SpellLevel(caster, spell->code);
	area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
	maxn = (uint32_t)S_SpellData(spell->code, level, 2);
	dur = S_SpellDuration(spell->code, level, G_UnitIsHero(st.entity));
	buffstr = G_AbilityLevel(spell->code, level)->buffID;
	if (!buffstr || strlen(buffstr) < 4) buffstr = "Bspl";
	buff = *((uint32_t const *)buffstr);
	if (!maxn) return;
	if (maxn > SPL_MAX_CANDS) {
		fprintf(stderr, "spirit_link: DataB %u exceeds scratch %d; clamping\n", (unsigned)maxn, SPL_MAX_CANDS);
		maxn = SPL_MAX_CANDS;
	}
	clicked = S_SpellIsAliveTarget(st.entity) && S_SpellAllowsTarget(spell->code, caster, st.entity);
	if (clicked) { cands[0] = st.entity; dists[0] = 0.0f; n = 1; }
	restmax = clicked ? maxn - 1 : maxn;
	FILTER_EDICTS(target, target != st.entity && S_SpellIsAliveTarget(target) && S_SpellAllowsTarget(spell->code, caster, target) &&
	              (dist = Vector2_distance(&target->s.origin2, &st.entity->s.origin2)) <= area) {
		rest = spirit_link_nearest_insert(cands + (clicked ? 1 : 0), dists + (clicked ? 1 : 0),
		                                  n - (clicked ? 1 : 0), restmax, target, dist);
		n = rest + (clicked ? 1 : 0);
	}
	FOR_LOOP(i, n) for (j = i + 1; j < n; j++)
		if (dists[j] < dists[i]) {
			float td = dists[i]; edict_t *te = cands[i];
			dists[i] = dists[j]; cands[i] = cands[j]; dists[j] = td; cands[j] = te;
		}
	if (n > maxn) n = maxn;
	FOR_LOOP(i, n) {
		unit_addtimedstatus(cands[i], buffstr, level, dur);
		spirit_link_store_code(cands[i], buff, spell->code);
		G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, cands[i], NULL, true);
	}
}

BZ_SIMPLE_SPELL_PROC(AbilitySpiritLink) { spirit_link_execute(caster, st, spell); }

static heroabilitystatus_t *spirit_link_slot(edict_t *unit) {
	if (!unit) return NULL;
	FOR_LOOP(i, MAX_UNIT_STATUSES)
		if (unit->abilstatus[i].level && unit->abilstatus[i].code == BZ_BSPL &&
		    (!unit->abilstatus[i].timestamp || unit->abilstatus[i].timestamp > G_Time()))
			return unit->abilstatus + i;
	return NULL;
}

static void spirit_link_strip(edict_t *unit) {
	FOR_LOOP(i, MAX_UNIT_STATUSES)
		if (unit->abilstatus[i].level && unit->abilstatus[i].code == BZ_BSPL)
			memset(unit->abilstatus + i, 0, sizeof(unit->abilstatus[i]));
}

/* Flat redirected share: never fatal — clamp to 1 HP and clear Bspl. */
static void spirit_link_apply_share(edict_t *unit, int amount) {
	if (!unit || amount <= 0 || M_IsDead(unit)) return;
	if (unit->health.value <= (float)amount) {
		G_SetHealth(unit, 1); spirit_link_strip(unit); return;
	}
	G_AddHealth(unit, -(float)amount);
}

/* Split DataA of post-mitigation damage across living allied Bspl holders; return primary take.
 * The 64-entry scratch cannot truncate real groups: selection above caps each
 * cast at authored DataB targets, far below this bound. */
int S_SpiritLinkRedirect(edict_t *target, edict_t *attacker, int damage) {
	static bool redirecting;
	heroabilitystatus_t *slot;
	uint32_t code, level, n = 0;
	float ratio;
	int shared, kept, portion;
	edict_t *linked[SPL_MAX_CANDS];
	(void)attacker;
	if (redirecting || !target || damage <= 0) return damage;
	slot = spirit_link_slot(target);
	if (!slot) return damage;
	code = slot->data ? slot->data : MAKEFOURCC('A', 's', 'p', 'l');
	level = slot->level ? slot->level : 1;
	ratio = S_SpellData(code, level, 1);
	if (ratio <= 0.0f) return damage;
	if (ratio > 1.0f) ratio = 1.0f;
	FILTER_EDICTS(other, S_SpellIsAliveTarget(other) && spirit_link_slot(other) &&
	              S_SpellIsFriend(target, other)) {
		if (n >= SPL_MAX_CANDS) continue;
		linked[n++] = other;
	}
	if (n < 1) return damage;
	shared = (int)((float)damage * ratio);
	kept = damage - shared;
	portion = shared / (int)n;
	redirecting = true;
	FOR_LOOP(i, n)
		if (linked[i] != target) spirit_link_apply_share(linked[i], portion);
	redirecting = false;
	return kept + portion;
}
