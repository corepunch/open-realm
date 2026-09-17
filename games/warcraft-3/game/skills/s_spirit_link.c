#include "s_skills.h"

#define BZ_BSPL MAKEFOURCC('B', 's', 'p', 'l') // rawcode; Spirit Link buff
#define SPL_MAX_CANDS 64 // units; gather cap when selecting DataB nearest in Area

static void spirit_link_store_code(LPEDICT unit, DWORD buff, DWORD code) {
	FOR_LOOP(i, MAX_UNIT_STATUSES)
		if (unit->abilstatus[i].level && unit->abilstatus[i].code == buff) {
			unit->abilstatus[i].data = code; break;
		}
}

/* Apply Bspl to up to DataB nearest valid units in Area of the click target. */
static void spirit_link_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
	DWORD level, buff, maxn, n = 0, j;
	FLOAT area, dur, dist;
	LPCSTR buffstr;
	LPEDICT cands[SPL_MAX_CANDS];
	FLOAT dists[SPL_MAX_CANDS];
	if (!st.entity || !spell) return;
	level = S_SpellLevel(caster, spell->code);
	area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
	maxn = (DWORD)S_SpellData(spell->code, level, 2);
	dur = S_SpellDuration(spell->code, level, G_UnitIsHero(st.entity));
	buffstr = G_AbilityLevel(spell->code, level)->buffID;
	if (!buffstr || strlen(buffstr) < 4) buffstr = "Bspl";
	buff = *((DWORD const *)buffstr);
	if (!maxn) return;
	FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellAllowsTarget(spell->code, caster, target) &&
	              (dist = Vector2_distance(&target->s.origin2, &st.entity->s.origin2)) <= area) {
		if (n >= SPL_MAX_CANDS) continue;
		cands[n] = target; dists[n] = dist; n++;
	}
	FOR_LOOP(i, n) for (j = i + 1; j < n; j++)
		if (dists[j] < dists[i]) {
			FLOAT td = dists[i]; LPEDICT te = cands[i];
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

static heroabilitystatus_t *spirit_link_slot(LPEDICT unit) {
	if (!unit) return NULL;
	FOR_LOOP(i, MAX_UNIT_STATUSES)
		if (unit->abilstatus[i].level && unit->abilstatus[i].code == BZ_BSPL &&
		    (!unit->abilstatus[i].timestamp || unit->abilstatus[i].timestamp > G_Time()))
			return unit->abilstatus + i;
	return NULL;
}

static void spirit_link_strip(LPEDICT unit) {
	FOR_LOOP(i, MAX_UNIT_STATUSES)
		if (unit->abilstatus[i].level && unit->abilstatus[i].code == BZ_BSPL)
			memset(unit->abilstatus + i, 0, sizeof(unit->abilstatus[i]));
}

/* Flat redirected share: never fatal — clamp to 1 HP and clear Bspl. */
static void spirit_link_apply_share(LPEDICT unit, int amount) {
	if (!unit || amount <= 0 || M_IsDead(unit)) return;
	if (unit->health.value <= (FLOAT)amount) {
		G_SetHealth(unit, 1); spirit_link_strip(unit); return;
	}
	G_AddHealth(unit, -(FLOAT)amount);
}

/* Split DataA of post-mitigation damage across living allied Bspl holders; return primary take. */
int S_SpiritLinkRedirect(LPEDICT target, LPEDICT attacker, int damage) {
	static BOOL redirecting;
	heroabilitystatus_t *slot;
	DWORD code, level, n = 0;
	FLOAT ratio;
	int shared, kept, portion;
	LPEDICT linked[SPL_MAX_CANDS];
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
	shared = (int)((FLOAT)damage * ratio);
	kept = damage - shared;
	portion = shared / (int)n;
	redirecting = true;
	FOR_LOOP(i, n)
		if (linked[i] != target) spirit_link_apply_share(linked[i], portion);
	redirecting = false;
	return kept + portion;
}
