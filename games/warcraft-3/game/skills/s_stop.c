#include "s_skills.h"

// Disabled until stop owns a custom stand move; Linux -Wall warns on unused static hooks.
// static umove_t stop_stand = { "stand", ai_stand, NULL, &a_stop};

ability_t a_stop = {
//    .cmd = build_command,
};

void order_stop(LPEDICT ent) {
    ent->patrol_a = NULL;
    unit_leavecombat(ent);
    ent->stand(ent);
}
