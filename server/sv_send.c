#include "server.h"

void SV_WriteGameCommand(LPSIZEBUF msg, LPCSTR command, sizeBuf_t const *payload) {
    if (!msg || !command || !payload) {
        return;
    }
    if (payload->cursize > 0x7fff) {
        fprintf(stderr,
                "SV_WriteGameCommand: payload too large for %s: %u bytes\n",
                command,
                (unsigned)payload->cursize);
        return;
    }
    MSG_WriteByte(msg, svc_gamecmd);
    MSG_WriteString(msg, command);
    MSG_WriteShort(msg, (int)payload->cursize);
    SZ_Write(msg, payload->data, payload->cursize);
}

void SV_Multicast(LPCVECTOR3 origin, multicast_t to) {
    FOR_LOOP(i, svs.num_clients) {
        LPCLIENT client = &svs.clients[i];
        SZ_Write(&client->netchan.message, sv.multicast.data, sv.multicast.cursize);
    }
    SZ_Clear(&sv.multicast);
}


/* Resolve an entity-backed unicast target without assuming that a game's
 * player number is identical to the engine connection slot.  UI/game APIs
 * often pass the connected client's own edict as the recipient; gameplay
 * sounds instead pass a world entity and use its player ownership. */
LPCLIENT SV_ClientForEntityRecipient(LPEDICT ent) {
    if (!ent) return NULL;

    FOR_LOOP(i, svs.num_clients) {
        LPCLIENT client = &svs.clients[i];
        if (client->state == cs_spawned && client->edict == ent)
            return client;
    }
    FOR_LOOP(i, svs.num_clients) {
        LPCLIENT client = &svs.clients[i];
        if (client->state == cs_spawned && client->playernum == ent->s.player)
            return client;
    }
    return NULL;
}

/* Encode one Quake 2-compatible sound event and deliver it to the selected
 * recipients.  CHAN_OWNER is a delivery policy and never crosses the wire. */
void SV_StartSound(LPCVECTOR3 origin, LPEDICT ent, int channel, int sound_index, FLOAT volume,
                   FLOAT attenuation, FLOAT timeofs) {
    DWORD flags = 0, ent_num = 0;
    VECTOR3 ent_origin;
    LPCVECTOR3 pos = origin;
    BOOL owner_only = channel & CHAN_OWNER;
    BOOL reliable = channel & CHAN_RELIABLE;
    BYTE *data;

    if (sound_index <= 0 || sound_index >= MAX_SOUNDS || volume < 0.0f || volume > 1.0f || attenuation < 0.0f ||
        attenuation > 4.0f || timeofs < 0.0f || timeofs > 0.255f) {
        fprintf(stderr, "SV_StartSound: invalid sound=%d volume=%.3f attenuation=%.3f offset=%.3f\n",
                sound_index, volume, attenuation, timeofs);
        return;
    }
    if (ent) {
        ent_num = NUM_FOR_EDICT(ent);
        if (!(owner_only && !origin)) flags |= SND_ENT;
        ent_origin = ent->s.origin;
        if (!pos) pos = &ent_origin;
    }
    if (origin) flags |= SND_POS;
    if (volume != DEFAULT_SOUND_PACKET_VOLUME) flags |= SND_VOLUME;
    if (attenuation != DEFAULT_SOUND_PACKET_ATTENUATION) flags |= SND_ATTENUATION;
    if (timeofs > 0.0f) flags |= SND_OFFSET;

    MSG_WriteByte(&sv.multicast, svc_sound);
    MSG_WriteByte(&sv.multicast, flags);
    MSG_WriteShort(&sv.multicast, sound_index);
    if (flags & SND_VOLUME) MSG_WriteByte(&sv.multicast, (int)(volume * 255.0f));
    if (flags & SND_ATTENUATION) MSG_WriteByte(&sv.multicast, (int)(attenuation * 64.0f));
    if (flags & SND_OFFSET) MSG_WriteByte(&sv.multicast, (int)(timeofs * 1000.0f));
    if (flags & SND_ENT) MSG_WriteShort(&sv.multicast, (int)((ent_num << 3) | (channel & 7)));
    if (flags & SND_POS) MSG_WritePos(&sv.multicast, pos);

    data = sv.multicast.data;
    if (owner_only) {
        LPCLIENT target = SV_ClientForEntityRecipient(ent);
        if (!target) {
            fprintf(stderr, "SV_StartSound: recipient unavailable for player=%u sound=%d\n",
                    ent ? (unsigned)ent->s.player : 0u, sound_index);
        } else {
            SZ_Write(&target->netchan.message, data, sv.multicast.cursize);
            if (reliable) Netchan_Transmit(NS_SERVER, &target->netchan);
        }
    } else {
        FOR_LOOP(i, svs.num_clients)
            if (svs.clients[i].state == cs_spawned) {
                SZ_Write(&svs.clients[i].netchan.message, data, sv.multicast.cursize);
                if (reliable) Netchan_Transmit(NS_SERVER, &svs.clients[i].netchan);
            }
    }
    SZ_Clear(&sv.multicast);
}
