#include "s_skills.h"

// Disabled until stop owns a custom stand move; Linux -Wall warns on unused static hooks.
// static umove_t stop_stand = { "stand", ai_stand, NULL, &a_stop};

void order_stop(LPEDICT ent) {
    ent->attackmove_waypoint = NULL;
    unit_leavecombat(ent);
    ent->stand(ent);
}

static void stop_command(LPEDICT ent) {
    FOR_SELECTED_UNITS(ent->client, e) {
        order_stop(e);
    }
}

ability_t a_stop = {
    .cmd = stop_command,
};
