#include "server.h"

/* Send one reliable transient attention marker without adding snapshot or save state. */
void SV_MinimapPing(LPEDICT ent, LPCVECTOR2 position, FLOAT duration, COLOR32 color, DWORD flags) {
    LPCLIENT client = NULL;

    if (!ent || !position || !isfinite(position->x) || !isfinite(position->y) || !isfinite(duration) ||
        duration <= 0.0f || duration > MINIMAP_PING_DURATION_MAX || flags > 0xff) {
        fprintf(stderr, "SV_MinimapPing: invalid duration=%.3f flags=0x%x\n", duration, (unsigned)flags);
        return;
    }
    client = SV_ClientForEntityRecipient(ent);
    if (!client) {
        fprintf(stderr, "SV_MinimapPing: no recipient for player=%u\n", (unsigned)ent->s.player);
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
    Netchan_Transmit(NS_SERVER, &client->netchan);
}
