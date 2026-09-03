#include "server.h"

/* Send one reliable transient attention marker without adding snapshot or save state. */
void SV_MinimapPing(LPEDICT ent, LPCVECTOR2 position, FLOAT duration, COLOR32 color, DWORD flags, int model) {
    LPCLIENT client = NULL;

    if (!ent || !position || !isfinite(position->x) || !isfinite(position->y) || !isfinite(duration) ||
        duration <= 0.0f || duration > MINIMAP_PING_DURATION_MAX ||
        model < 0 || model >= MAX_MODELS || flags > 0xff) {
        fprintf(stderr, "SV_MinimapPing: invalid duration=%.3f flags=0x%x model=%d\n",
                duration, (unsigned)flags, model);
        return;
    }
    FOR_LOOP(i, svs.num_clients)
        if (svs.clients[i].state == cs_spawned && svs.clients[i].playernum == ent->s.player) {
            client = &svs.clients[i];
            break;
        }
    if (!client) {
        fprintf(stderr, "SV_MinimapPing: no client for player=%u\n", (unsigned)ent->s.player);
        return;
    }

    MSG_WriteByte(&client->netchan.message, svc_minimap_ping);
    MSG_WriteFloat(&client->netchan.message, position->x);
    MSG_WriteFloat(&client->netchan.message, position->y);
    MSG_WriteFloat(&client->netchan.message, duration);
    MSG_WriteByte(&client->netchan.message, color.r);
    MSG_WriteByte(&client->netchan.message, color.g);
    MSG_WriteByte(&client->netchan.message, color.b);
    MSG_WriteByte(&client->netchan.message, color.a);
    MSG_WriteByte(&client->netchan.message, (int)flags);
    MSG_WriteShort(&client->netchan.message, model);
    Netchan_Transmit(NS_SERVER, &client->netchan);
}
