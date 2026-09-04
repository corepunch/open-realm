#include "s_skills.h"

/* Siphon Mana (ANdr): channeled unit-target spell.  Drains mana from the target
 * and transfers it to the caster over time.  SPELL_CHANNEL flag locks the caster
 * in place; movement or stun cancels the drain. */

#define ID_SIPHON_MANA MAKEFOURCC('A', 'N', 'd', 'r')

void siphon_mana_think(LPEDICT ent) {
    LPEDICT caster = ent->owner;
    LPEDICT target = ent->goalentity;
    DWORD now = G_Time();

    if (!target || !target->inuse || M_IsDead(target)) {
        S_SpellCancelChannel(caster);
        G_FreeEdict(ent);
        return;
    }

    if (ent->freetime && now < ent->freetime)
        return;

    /* Drain per tick. */
    FLOAT drain = ent->velocity; /* velocity = mana drained per second */
    if (target->mana.value > 0) {
        FLOAT transfer = MIN(drain, target->mana.value);
        target->mana.value -= transfer;
        caster->mana.value = MIN(caster->mana.max_value, caster->mana.value + transfer);
    }

    if (ent->resources > 0)
        ent->resources--;
    if (ent->resources == 0) {
        S_SpellCancelChannel(caster);
        G_FreeEdict(ent);
        return;
    }
    ent->freetime = now + 1000;
}

static void siphon_mana_execute(LPEDICT caster, spellTarget_t st, spell_info_t const *spell) {
    LPEDICT target = st.entity;
    DWORD level = S_SpellLevel(caster, spell->code);
    FLOAT mana_per_second = MAX(1.0f, S_SpellData(spell->code, level, 1)); /* DataA = Mana Per Second */
    DWORD ticks = (DWORD)MAX(1.0f, S_SpellData(spell->code, level, 2));    /* DataB = Duration (seconds) */
    LPEDICT thinker;

    /* Cannot drain from an empty mana pool. */
    if (target->mana.value <= 0)
        return;

    thinker = G_Spawn();
    thinker->owner = caster;
    thinker->goalentity = target;
    thinker->velocity = mana_per_second;
    thinker->resources = ticks;
    thinker->think = siphon_mana_think;
    thinker->freetime = G_Time() + 1000;
}

static spell_info_t spell_siphon_mana = {
    .code = ID_SIPHON_MANA,
    .name = "Siphon Mana",
    .target_type = SPELL_TARGET_UNIT,
    .flags = SPELL_CHANNEL,
    .execute = siphon_mana_execute,
};

ability_t a_siphon_mana = {
    .cmd = spell_cmd,
    .spell = &spell_siphon_mana,
};
