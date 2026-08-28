#include "g_local.h"

void tree_decay1(LPEDICT self);
void tree_stand(LPEDICT self);

static umove_t tree_move_birth = { "birth", ai_idle, tree_stand };
static umove_t tree_move_stand = { "stand", ai_idle, tree_stand };
static umove_t tree_move_pain = { "stand hit", ai_pain, tree_stand };
static umove_t tree_move_death = { "death", NULL, tree_decay1 };

void tree_decay1(LPEDICT self) {
//    self->monsterinfo.currentmove = &tree_move_decay1;
    self->aiflags |= AI_HOLD_FRAME;
}

void tree_pain(LPEDICT self) {
    unit_setmove(self, &tree_move_pain);
}

void tree_stand(LPEDICT self) {
    G_DestructableStartAliveAnimation(self, false);
}

void G_DestructableStartAliveAnimation(LPEDICT self, BOOL birth) {
    unit_setmove(self, birth ? &tree_move_birth : &tree_move_stand);
    if (self->animation)
        self->s.frame = self->animation->interval[0];
}

void G_DestructableStartDeathAnimation(LPEDICT self) {
    unit_setmove(self, &tree_move_death);
    /* Begin the death sequence in the transition itself. Missing model
     * sequences leave animation NULL but do not block lifecycle processing. */
    if (self->animation)
        self->s.frame = self->animation->interval[0];
    self->svflags |= SVF_DEADMONSTER;
}

/* Legacy callback entry point used by script/native paths. Destructable death
 * itself is owned by G_KillDestructable and does not depend on this callback. */
void tree_die(LPEDICT self, LPEDICT attacker) {
    G_KillDestructable(self, attacker);
}

void tree_birth(LPEDICT self) {
    G_DestructableStartAliveAnimation(self, true);
}

void SP_monster_tree(LPEDICT self) {
    self->stand = tree_stand;
    self->birth = tree_birth;
    self->pain = tree_pain;
    self->die = tree_die;

    unit_setmove(self, &tree_move_stand);

    self->think = monster_think;
    monster_start(self);
}
