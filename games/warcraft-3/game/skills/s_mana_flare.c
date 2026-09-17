#include "s_skills.h"

#define ID_MANA_FLARE MAKEFOURCC('A', 'm', 'f', 'l') // rawcode; Faerie Dragon Mana Flare
#define ID_BMFL MAKEFOURCC('B', 'm', 'f', 'l') // rawcode; Mana Flare caster buff

static void mana_flare_strip(LPEDICT unit) {
	FOR_LOOP(i, MAX_UNIT_STATUSES)
		if (unit->abilstatus[i].level && unit->abilstatus[i].code == ID_BMFL)
			memset(unit->abilstatus + i, 0, sizeof(unit->abilstatus[i]));
	G_InvalidateUnitInfoPanel(unit);
}

static heroabilitystatus_t *mana_flare_status(LPEDICT unit) {
	FOR_LOOP(i, MAX_UNIT_STATUSES)
		if (unit->abilstatus[i].level && unit->abilstatus[i].code == ID_BMFL)
			return unit->abilstatus + i;
	return NULL;
}

static BOOL mana_flare_can_splash(LPEDICT flare, LPEDICT primary, LPEDICT target, FLOAT splash_r, BOOL mana_only) {
	if (!target || target == primary || target == flare || !S_SpellIsAliveTarget(target)) return false;
	if (!S_SpellIsEnemy(flare, target)) return false;
	if (Vector2_distance(&target->s.origin2, &primary->s.origin2) > splash_r) return false;
	if (mana_only && target->mana.max_value <= 0.0f) return false;
	return S_SpellAllowsTarget(ID_MANA_FLARE, flare, target);
}

static int mana_flare_damage(LPEDICT flare, LPEDICT victim, FLOAT cost) {
	DWORD level = MAX(1u, G_UnitAbilityLevel(flare, ID_MANA_FLARE));
	BOOL hero = G_UnitIsHero(victim);
	FLOAT per = S_SpellData(ID_MANA_FLARE, level, hero ? 2 : 1);
	FLOAT cap = S_SpellData(ID_MANA_FLARE, level, hero ? 4 : 3);
	return (int)MIN(cap, cost * per);
}

/* Name=Mana Flare
 * Channel: Bmfl on caster, DataE armor, flare enemies that spend mana in Area.
 */
BZ_ABILITY_PROC(CAbilityManaFlare) {
	DWORD code = call && call->item && call->item->code ? call->item->code : ID_MANA_FLARE;
	DWORD level;
	LPCSTR buff;
	switch (msg) {
	case A_EXECUTE:
		level = S_SpellLevel(ent, code);
		buff = G_AbilityLevel(code, level)->buffID;
		if (!buff || strlen(buff) < 4) buff = "Bmfl";
		unit_addtimedstatus(ent, buff, level, S_SpellDuration(code, level, G_UnitIsHero(ent)));
		return true;
	case A_UPDATE:
		if (ent && ent->channel.code == code && !G_UnitStatusLevel(ent, ID_BMFL))
			S_SpellCancelChannel(ent);
		return true;
	case A_CANCEL:
	case A_DISABLE:
	case A_UNIT_REMOVE:
		if (G_UnitStatusLevel(ent, ID_BMFL)) mana_flare_strip(ent);
		return true;
	case A_MOVE_LEAVE:
		if (ent && ent->channel.code == code) {
			S_SpellCancelChannel(ent);
			return true;
		}
		return false;
	default:
		return CAbilitySimpleSpell(ent, msg, call);
	}
}

/* DataE armor while Bmfl is active; ubertip binds armor to DataE. */
FLOAT S_ManaFlareArmorBonus(LPCEDICT unit) {
	DWORD level = G_UnitStatusLevel(unit, ID_BMFL);
	return level ? S_SpellData(ID_MANA_FLARE, level, 5) : 0.0f;
}

/* After a successful spell_commit: each enemy Amfl in Area may flare the caster. */
void S_ManaFlareOnCast(LPEDICT caster, DWORD spell_code, DWORD spell_level) {
	FLOAT cost;
	if (!caster || !spell_code || !S_SpellIsAliveTarget(caster)) return;
	cost = S_SpellNumber(spell_code, ABILITY_NUMBER_COST, MAX(1u, spell_level));
	if (cost <= 0.0f) return;

	FILTER_EDICTS(flare, S_SpellIsAliveTarget(flare) && G_UnitStatusLevel(flare, ID_BMFL) &&
				  S_SpellIsEnemy(flare, caster)) {
		heroabilitystatus_t *st;
		DWORD level, now, gate;
		FLOAT area, splash_r;
		BOOL mana_only;
		int damage;

		level = MAX(1u, G_UnitAbilityLevel(flare, ID_MANA_FLARE));
		area = S_SpellNumber(ID_MANA_FLARE, ABILITY_NUMBER_AREA, level);
		if (area > 0.0f && Vector2_distance(&flare->s.origin2, &caster->s.origin2) > area) continue;
		if (!S_SpellAllowsTarget(ID_MANA_FLARE, flare, caster)) continue;

		st = mana_flare_status(flare);
		now = G_Time();
		gate = (DWORD)(S_SpellNumber(ID_MANA_FLARE, ABILITY_NUMBER_CAST, level) * 1000.0f);
		if (st && gate && st->data && now < st->data + gate) continue;
		if (st) st->data = now;

		damage = mana_flare_damage(flare, caster, cost);
		if (damage > 0) S_SpellDamage(caster, flare, damage);

		splash_r = S_SpellNumber(ID_MANA_FLARE, ABILITY_NUMBER_RANGE, level);
		mana_only = S_SpellData(ID_MANA_FLARE, level, 6) > 0.0f;
		if (splash_r <= 0.0f || damage <= 0) continue;
		FILTER_EDICTS(splash, mana_flare_can_splash(flare, caster, splash, splash_r, mana_only))
			S_SpellDamage(splash, flare, mana_flare_damage(flare, splash, cost));
	}
}
