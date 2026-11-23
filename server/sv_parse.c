#include "server.h"

void SV_ParseCameraPosition(sizeBuf_t *msg, client_t *client) {
    edict_t *clent = client->edict;
    float x = MSG_ReadFloat(msg);
    float y = MSG_ReadFloat(msg);
    /* A map restart can leave the previous world's camera packet queued before
     * the replacement client completes `begin`; consume it without addressing
     * the new world's still-unassigned player edict. */
    if (client->state != cs_spawned || !clent) return;
    ge->ClientInput(clent, &(inputCmd_t){ .action = BZ_INPUT_FOCUS, .focus = {x, y} });
}

void SV_ParseClientMessage(sizeBuf_t *msg, client_t *client) {
    uint8_t pack_id = 0;
    while (MSG_Read(msg, &pack_id, 1)) {
        switch (pack_id) {
            case clc_input: {
                inputCmd_t cmd;
                if (!MSG_ReadInput(msg, &cmd)) {
                    fprintf(stderr, "SV_ParseClientMessage: invalid controller input\n");
                    msg->readcount = msg->cursize;
                    return;
                }
                /* Camera preferences precede `begin` on the same ordered
                 * message so the game can apply them before map scripts.
                 * No gameplay input is accepted until the client is spawned. */
                if (client->edict && (client->state == cs_spawned ||
                    (client->state == cs_connected && cmd.action == BZ_INPUT_CAMERA_PREFERENCE)))
                    ge->ClientInput(client->edict, &cmd);
                break;
            }
            case clc_camera_position:
                SV_ParseCameraPosition(msg, client);
                break;;
            case clc_stringcmd:
                SV_ExecuteUserCommand(msg, client);
                break;
                
            // Phase 8: Unit UI data requests
            case clc_request_unit_ui:
                SV_HandleUnitUIRequest(client, msg);
                break;
                
            default:
                fprintf(stderr, "Unknown message %d\n", pack_id);
                return;
        }
    }
}
