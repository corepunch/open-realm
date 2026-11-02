#include "s_skills.h"

#define ID_MANA_FLARE MAKEFOURCC('A', 'm', 'f', 'l') // rawcode; Faerie Dragon Mana Flare
#define ID_BMFL MAKEFOURCC('B', 'm', 'f', 'l') // rawcode; Mana Flare caster buff

static void mana_flare_strip(edict_t *unit) {
	FOR_LOOP(i, unit->unit_state ? MAX_UNIT_STATUSES : 0)
		if (unit->unit_state->statuses[i].level && unit->unit_state->statuses[i].code == ID_BMFL)
			unit_removestatus(unit, unit->unit_state->statuses + i, STATUS_REMOVE_SCRIPT);
	G_AddUnitAnimationProperties(unit, "channel", false);
	G_InvalidateUnitInfoPanel(unit);
	G_InvalidateUnitCommands(unit);
}

bool S_ManaFlareActive(edict_t const *unit) {
	return unit && G_UnitStatusLevel(unit, ID_BMFL) > 0;
}

bool S_ManaFlareAllowsOrder(edict_t const *unit, cstring_t order) {
	if (!S_ManaFlareActive(unit)) return true;
	/* The active toggle remains available so the player can end the status. */
	return order && ((!strncmp(order, "Amfl", 4) && (!order[4] || order[4] == ':')) ||
		!strcmp(order, "amfl") || !strcmp(order, "manaflareon") || !strcmp(order, "manaflareoff"));
}

static void mana_flare_present(edict_t *unit, bool active) {
	G_AddUnitAnimationProperties(unit, "channel", active);
}

static heroabilitystatus_t *mana_flare_status(edict_t *unit) {
	FOR_LOOP(i, unit && unit->unit_state ? MAX_UNIT_STATUSES : 0)
		if (unit->unit_state->statuses[i].level && unit->unit_state->statuses[i].code == ID_BMFL)
			return unit->unit_state->statuses + i;
	return NULL;
}

/* Preflight the exact first BuffID token used by execute. A full status list
 * must reject the cast before spell_commit spends mana or starts cooldown. */
static bool mana_flare_can_apply(edict_t *unit, uint32_t code) {
	uint32_t level;
	cstring_t buff;
	wc3_status_apply_check_t result;
	if (!unit) return false;
	unit_updatestatuses(unit);
	if (mana_flare_status(unit)) return true; /* toggling off needs no free slot */
	level = S_SpellLevel(unit, code);
	buff = S_SpellBuffId(code, level);
	if (!buff || strlen(buff) < 4) buff = "Bmfl";
	result = unit_status_checkapplication(unit, &(status_application_t){
		.buff = buff, .level = level
	});
	return result == WC3_STATUS_APPLY_FREE_SLOT || result == WC3_STATUS_APPLY_REUSE;
}

static bool mana_flare_can_splash(edict_t *flare, edict_t *primary, edict_t *target, float splash_r, bool mana_only) {
	if (!target || target == primary || target == flare || !S_SpellIsAliveTarget(target)) return false;
	if (!S_SpellIsEnemy(flare, target)) return false;
	if (Vector2_distance(&target->s.origin2, &primary->s.origin2) > splash_r) return false;
	if (mana_only && target->mana.max_value <= 0.0f) return false;
	return S_SpellAllowsTarget(ID_MANA_FLARE, flare, target);
}

static int mana_flare_damage(edict_t *flare, edict_t *victim, float cost) {
	uint32_t level = MAX(1u, G_UnitAbilityLevel(flare, ID_MANA_FLARE));
	bool hero = G_UnitIsHero(victim);
	float per = S_SpellData(ID_MANA_FLARE, level, hero ? 2 : 1);
	float cap = S_SpellData(ID_MANA_FLARE, level, hero ? 4 : 3);
	return (int)MIN(cap, cost * per);
}

/* Name=Mana Flare
 * Timed Bmfl status on caster, DataE armor, flare enemies that spend mana in Area.
 */
BZ_ABILITY_PROC(CAbilityManaFlare) {
	uint32_t code = call && call->item && call->item->code ? call->item->code : ID_MANA_FLARE;
	uint32_t level;
	cstring_t buff;
	switch (msg) {
	case A_VALIDATE:
		return mana_flare_can_apply(ent, code);
	case A_TOGGLE_ON:
		return mana_flare_status(ent) != NULL;
	case A_ORDER:
		/* Immediate orders are explicit state requests, unlike the HUD's
		 * toggle command. Check ability ownership before executing either. */
		if (!ent || !call || !call->order || !G_UnitAbilityLevel(ent, code)) return false;
		if (!strcmp(call->order, "manaflareon"))
			return mana_flare_status(ent) || S_CastNoTargetSpell(ent, code);
		if (!strcmp(call->order, "manaflareoff"))
			return !mana_flare_status(ent) || S_CastNoTargetSpell(ent, code);
		return false;
	case A_STATUS_REMOVE:
		if (call && call->status.slot && call->status.slot->code == ID_BMFL) {
			mana_flare_present(ent, false);
			G_InvalidateUnitCommands(ent);
		}
		return false;
	case A_EXECUTE:
		if (mana_flare_status(ent)) {
			mana_flare_strip(ent);
			return true;
		}
		level = S_SpellLevel(ent, code);
		buff = S_SpellBuffId(code, level);
		if (!buff || strlen(buff) < 4) buff = "Bmfl";
		{
			heroabilitystatus_t *status = S_SpellApplyTimedStatusFromAbility(
				ent, code, level, buff, S_SpellHeroDuration(code, level, ent));
			if (!status) return false; /* Defensive: a failed application is never success. */
			status->buff_flags |= WC3_STATUS_BUFF_POSITIVE | WC3_STATUS_BUFF_MAGICAL |
				WC3_STATUS_BUFF_TARGET_ART;
			unit_status_enabletargetart(ent, status, "overhead");
			mana_flare_present(ent, true);
			order_stop_cleanup(ent);
			G_ClearActiveUnitOrder(ent);
			G_InvalidateUnitCommands(ent);
		}
		return true;
	case A_UPDATE:
		if (ent) {
			bool active = mana_flare_status(ent) != NULL;
			mana_flare_present(ent, active);
		}
		return true;
	case A_CANCEL:
	case A_DISABLE:
	case A_UNIT_REMOVE:
		if (G_UnitStatusLevel(ent, ID_BMFL)) mana_flare_strip(ent);
		return true;
	default:
		return CAbilitySimpleSpell(ent, msg, call);
	}
}

/* DataE armor while Bmfl is active; ubertip binds armor to DataE. */
float S_ManaFlareArmorBonus(edict_t const *unit) {
	uint32_t level = G_UnitStatusLevel(unit, ID_BMFL);
	return level ? S_SpellData(ID_MANA_FLARE, level, 5) : 0.0f;
}

/* After a successful spell_commit: each enemy Amfl in Area may flare the caster. */
void S_ManaFlareOnCast(edict_t *caster, uint32_t spell_code, uint32_t spell_level) {
	float cost;
	if (!caster || !spell_code || !S_SpellIsAliveTarget(caster)) return;
	cost = S_SpellNumber(spell_code, ABILITY_NUMBER_COST, MAX(1u, spell_level));
	if (cost <= 0.0f) return;

	FILTER_EDICTS(flare, S_SpellIsAliveTarget(flare) && G_UnitStatusLevel(flare, ID_BMFL) &&
				  S_SpellIsEnemy(flare, caster)) {
		heroabilitystatus_t *st;
		uint32_t level, now, gate;
		float area, splash_r;
		bool mana_only;
		int damage;

		level = MAX(1u, G_UnitAbilityLevel(flare, ID_MANA_FLARE));
		area = S_SpellNumber(ID_MANA_FLARE, ABILITY_NUMBER_AREA, level);
		if (area > 0.0f && Vector2_distance(&flare->s.origin2, &caster->s.origin2) > area) continue;
		if (!S_SpellAllowsTarget(ID_MANA_FLARE, flare, caster)) continue;

		st = mana_flare_status(flare);
		now = G_Time();
		gate = (uint32_t)(S_SpellNumber(ID_MANA_FLARE, ABILITY_NUMBER_CAST, level) * 1000.0f);
		/* next_tick is a deadline, not a last-fire timestamp: time zero and
		 * save/load both preserve the full initial cooldown. */
		if (st && st->next_tick && now < st->next_tick) continue;
		if (st) st->next_tick = gate ? now + gate : 0;

		damage = mana_flare_damage(flare, caster, cost);
		if (damage > 0) S_SpellDamage(caster, flare, damage);

		splash_r = S_SpellNumber(ID_MANA_FLARE, ABILITY_NUMBER_RANGE, level);
		mana_only = S_SpellData(ID_MANA_FLARE, level, 6) > 0.0f;
		if (splash_r <= 0.0f || damage <= 0) continue;
		FILTER_EDICTS(splash, mana_flare_can_splash(flare, caster, splash, splash_r, mana_only))
			S_SpellDamage(splash, flare, mana_flare_damage(flare, splash, cost));
	}
}
