#include "s_skills.h"

#define ID_TIMED_LIFE "BTLF"

static void mirror_image_spawn(LPEDICT caster, uint32_t index, uint32_t count, float duration) {
    VECTOR2 loc;
    float angle;
    LPEDICT image;

    angle = count ? (2.0f * (float)M_PI * (float)index) / (float)count : 0.0f;
    loc = caster->s.origin2;
    loc.x += cosf(angle) * MAX(64.0f, caster->collision + 32.0f);
    loc.y += sinf(angle) * MAX(64.0f, caster->collision + 32.0f);
    SP_FindEmptySpaceAround(caster, caster->class_id, &loc, &angle);

    image = SP_SpawnAtLocation(caster->class_id, caster->s.player, &loc);
    if (!image) return;

    /* Mirror Image copies the visible Hero/unit state but is a summoned
     * illusion: it must not reserve food or become campaign-persistent Hero
     * progression.  The runtime marker is enough for JASS identity checks and
     * later damage/parity work without changing the network contract. */
    image->owner = caster;
    image->aiflags |= AI_ILLUSION;
    image->hero = caster->hero;
    image->hero.suspend_xp = true;
    memcpy(image->heroabilities, caster->heroabilities, sizeof(image->heroabilities));
    image->health = caster->health;
    image->mana = caster->mana;
    image->unit_color = caster->unit_color;
    image->s.effect_flags = (image->s.effect_flags & ~EFX_TEAM_COLOR_MASK) |
        (caster->s.effect_flags & EFX_TEAM_COLOR_MASK);
    image->s.angle = caster->s.angle;
    if (image->stand) image->stand(image);
    if (duration > 0.0f) unit_addtimedstatus(image, ID_TIMED_LIFE, 1, duration);

    /* Summon triggers are how campaign scripts discover the newly-created
     * image.  The summoner is the trigger unit and GetSummonedUnit resolves
     * the event source. */
    G_PublishSummonEvents(caster, image);
}

BZ_SIMPLE_SPELL_PROC(AbilityMirrorImage) {
    uint32_t level;
    uint32_t count;
    float duration;
    (void)st;

    if (!caster || !spell) return;
    level = S_SpellLevel(caster, spell->code);
    count = (uint32_t)MAX(0.0f, S_SpellData(spell->code, level, 1));
    duration = S_SpellDuration(spell->code, level, false);
    if (!count) count = 1;

    FOR_LOOP(i, count)
        mirror_image_spawn(caster, i, count, duration);
}
