#include "s_skills.h"

// Disabled until stop owns a custom stand move; Linux -Wall warns on unused static hooks.
// static umove_t stop_stand = { "stand", ai_stand, NULL, CAbilityStop};

static void order_stop_state(edict_t *ent, bool preserve_queue) {
    if (S_GoldMineWorkerIsInside(ent))
        return;
    /* Channeling can retain the idle move, so Stop must cancel even without a move-leave notification. */
    S_SpellCancelChannel(ent);
    ent->movement.attackmove_waypoint = NULL;
    ent->movement.patrol_a = NULL;
    ent->movement.patrol_b = NULL;
    ent->movement.patrol_target = NULL;
    ent->movement.follow_target = NULL;
    ent->movement.holding_position = false;
    unit_leavecombat(ent);
    if (preserve_queue) unit_stand_no_queue(ent);
    else ent->stand(ent);
}

void order_stop(edict_t *ent) {
    G_ClearUnitOrderQueue(ent);
    order_stop_state(ent, false);
}

void order_stop_queued(edict_t *ent) {
    order_stop_state(ent, true);
}

BZ_COMMAND_PROC(AbilityStop) {
    gameClient_t *client = clent->client;
    FOR_CONTROLLABLE_SELECTED_UNITS(client, e) {
        if (client->menu.order_queued && G_UnitHasActiveOrder(e)) {
            if (G_QueueUnitOrder(e, "stop", UNIT_ORDER_TARGET_NONE, NULL, NULL,
                                 client->ps.number, 0.0f, 0)) {
                G_PublishIssuedImmediateOrder(e, G_OrderId("stop"), client->ps.number, "stop");
            }
        } else {
            unit_issueimmediateorder(e, "stop");
        }
    }
}
