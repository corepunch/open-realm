#include "s_skills.h"

static void ai_holdpos_stand(LPEDICT self) {
    if (!G_ShouldAcquireThisFrame(self))
        return;
    LPEDICT enemy = G_FindNearestEnemy(self, self->attack1.range);
    if (enemy) {
        order_attack(self, enemy);
    }
}

umove_t holdpos_move_stand = { "stand", ai_holdpos_stand, unit_stand };
umove_t holdpos_move_stand_ready = { "stand ready", ai_holdpos_stand, unit_stand };

static void holdpos_command(LPEDICT ent) {
    FOR_SELECTED_UNITS(ent->client, e) {
        e->holding_position = true;
        unit_leavecombat(e);
        unit_stand(e);
    }
}

ability_t a_holdpos = {
    .cmd = holdpos_command,
};
