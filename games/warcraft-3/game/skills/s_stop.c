#include "s_skills.h"

// Disabled until stop owns a custom stand move; Linux -Wall warns on unused static hooks.
// static umove_t stop_stand = { "stand", ai_stand, NULL, CAbilityStop};

void order_stop(edict_t *ent) {
    if (S_GoldMineWorkerIsInside(ent))
        return;
    G_ClearUnitOrderQueue(ent);
    /* Channeling can retain the idle move, so Stop must cancel even without a move-leave notification. */
    S_SpellCancelChannel(ent);
    ent->movement.attackmove_waypoint = NULL;
    ent->movement.patrol_a = NULL;
    ent->movement.patrol_b = NULL;
    ent->movement.patrol_target = NULL;
    ent->movement.follow_target = NULL;
    ent->movement.holding_position = false;
    unit_leavecombat(ent);
    ent->stand(ent);
}

BZ_COMMAND_PROC(AbilityStop) {
    FOR_CONTROLLABLE_SELECTED_UNITS(clent->client, e) {
        unit_issueimmediateorder(e, "stop");
    }
}
