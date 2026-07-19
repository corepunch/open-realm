/*
 * sv_ents.c — Server-side entity snapshot and synchronization.
 *
 * Each frame the server collects the set of entities visible to each client
 * (SV_BuildClientFrame), compares it to the previous frame, and sends only
 * the changed fields using delta compression (SV_EmitPacketEntities).
 *
 * The result is an svc_frame message containing svc_playerinfo (camera /
 * player state) followed by svc_packetentities (entity deltas).  The client
 * applies these deltas in cl_parse.c to keep its local entity table up to
 * date.
 */
#include <stdlib.h>

#include "server.h"

#define VISUAL_DISTANCE 1500
#define HIGH_NUMBER 9999
#define OWNED_ENTITY_SCORE_BIAS 1000000000.0f

typedef struct {
    edict_t *edict;
    FLOAT score;
} visibleEntityCandidate_t;

/* Determine whether a client should receive updates for the given entity.
 * Always true for the client's own player-owned entities; otherwise based
 * on a simple distance check against the client's camera position. */
static bool SV_CanClientSeeEntity(LPCCLIENT client, LPCEDICT edict) {
#ifdef WOW
    (void)client;
    (void)edict;
    return true;
#else
    edict_t *clent = client->edict;
    if (edict->s.player == clent->client->ps.number)
        return true;
    if (ge->CanSeeEntity) {
        return ge->CanSeeEntity(clent->client->ps.number, edict);
    }
    if (fabs(edict->s.origin.x - clent->client->ps.origin.x) > VISUAL_DISTANCE)
        return false;
    if (fabs(edict->s.origin.y - clent->client->ps.origin.y) > VISUAL_DISTANCE)
        return false;
    return true;
#endif
}

static FLOAT SV_ClientEntityVisibilityScore(LPCCLIENT client, LPCEDICT edict) {
#ifdef WOW
    (void)client;
    (void)edict;
    return 0.0f;
#else
    edict_t *clent = client->edict;
    FLOAT dx = edict->s.origin.x - clent->client->ps.origin.x;
    FLOAT dy = edict->s.origin.y - clent->client->ps.origin.y;
    FLOAT score = dx * dx + dy * dy;

    if (edict->s.player == clent->client->ps.number) {
        score -= OWNED_ENTITY_SCORE_BIAS;
    }
    return score;
#endif
}

static int SV_CompareCandidateByNumber(const void *a, const void *b) {
    visibleEntityCandidate_t const *ea = a;
    visibleEntityCandidate_t const *eb = b;
    return ea->edict->s.number - eb->edict->s.number;
}

static void SV_AddVisibleEntityCandidate(visibleEntityCandidate_t *candidates,
                                         int *num_candidates,
                                         edict_t *edict,
                                         FLOAT score)
{
    int worst = 0;

    if (*num_candidates < MAX_PACKET_ENTITIES) {
        candidates[*num_candidates] = (visibleEntityCandidate_t){ edict, score };
        (*num_candidates)++;
        return;
    }

    for (int i = 1; i < *num_candidates; i++) {
        if (candidates[i].score > candidates[worst].score) {
            worst = i;
        }
    }
    if (score >= candidates[worst].score) {
        return;
    }
    candidates[worst] = (visibleEntityCandidate_t){ edict, score };
}

LPENTITYSTATE SV_NextClientEntity(void) {
    int index = svs.next_client_entities++ % svs.num_client_entities;
    return &svs.client_entities[index];
}

/* Populate a client frame snapshot with all entities visible to the client.
 * The snapshot records the player state and a list of entity states that will
 * be delta-encoded and sent by SV_WriteFrameToClient. */
void SV_BuildClientFrame(LPCLIENT client) {
    edict_t *clent = client->edict;
    LPCLIENTFRAME frame = &client->frames[sv.framenum & UPDATE_MASK];
    /* MAX_PACKET_ENTITIES (16384) makes this array too large for the stack
     * (~256KB per call, every client, every frame) - keep it static instead. */
    static visibleEntityCandidate_t candidates[MAX_PACKET_ENTITIES];
    int num_candidates = 0;
#ifdef WOW
    int first_entity = 0;
#else
    int first_entity = 1;
#endif

    frame->ps = clent->client->ps;
    frame->num_entities = 0;
    if (!svs.client_entities || svs.num_client_entities == 0) {
        frame->first_entity = 0;
        return;
    }
    frame->first_entity = svs.next_client_entities;
    for (int index = first_entity; index < ge->num_edicts; index++) {
        edict_t *edict = EDICT_NUM(index);
        if (!edict->inuse)
            continue;
        if (edict->svflags & SVF_NOCLIENT)
            continue;
        if (!edict->s.model && !edict->s.sound && !edict->s.event)
            continue;
        if (!SV_CanClientSeeEntity(client, edict) && index > ge->max_clients)
            continue;
        SV_AddVisibleEntityCandidate(candidates,
                                     &num_candidates,
                                     edict,
                                     SV_ClientEntityVisibilityScore(client, edict));
    }

    qsort(candidates,
          (size_t)num_candidates,
          sizeof(candidates[0]),
          SV_CompareCandidateByNumber);

    for (int index = 0; index < num_candidates; index++) {
        edict_t *edict = candidates[index].edict;
        LPENTITYSTATE state = SV_NextClientEntity();
        *state = edict->s;
        if (edict->selected & (1 << clent->client->ps.number)) {
            state->renderfx |= RF_SELECTED;
        }
        frame->num_entities++;
    }
}

/* Write the delta-compressed entity list for this frame.
 * Entities present in both old and new frames are written as deltas.
 * New entities are written against the server baseline.
 * Entities removed since the last frame receive a U_REMOVE flag. */
void SV_EmitPacketEntities(LPCCLIENTFRAME from, LPCCLIENTFRAME to, LPSIZEBUF msg) {
    int const from_num_entities = from ? from->num_entities : 0;
    entityState_t nullstate = { 0 };
    int debug_entities = Cvar_Integer("sv_debug_entities", 0);
    int added = 0;
    int removed = 0;
    int changed = 0;

    MSG_WriteByte (msg, svc_packetentities);

    for (int newindex = 0, oldindex = 0;
         newindex < to->num_entities ||
         oldindex < from_num_entities;)
    {
        LPENTITYSTATE newent = NULL;
        LPENTITYSTATE oldent = NULL;
        int newnum = 0, oldnum = 0;
        if (newindex >= to->num_entities) {
            newnum = HIGH_NUMBER;
        } else{
            newent = &svs.client_entities[(to->first_entity+newindex)%svs.num_client_entities];
            newnum = newent->number;
        }
        if (oldindex >= from_num_entities) {
            oldnum = HIGH_NUMBER;
        } else {
            oldent = &svs.client_entities[(from->first_entity+oldindex)%svs.num_client_entities];
            oldnum = oldent->number;
        }
        if (newnum == oldnum) {
            if (debug_entities && oldent && newent &&
                (oldent->model != newent->model ||
                 oldent->class_id != newent->class_id)) {
                fprintf(stderr,
                        "SV entity change frame=%u ent=%d model=%u->%u class=%u->%u\n",
                        (unsigned)sv.framenum,
                        newnum,
                        (unsigned)oldent->model,
                        (unsigned)newent->model,
                        (unsigned)oldent->class_id,
                        (unsigned)newent->class_id);
                changed++;
            }
            MSG_WriteDeltaEntity(msg, oldent, newent, false);
            oldindex++;
            newindex++;
            continue;
        }
        if (newnum < oldnum) { // this is a new entity, send it from the baseline
            LPCENTITYSTATE base = &nullstate;
            if (sv.baselines && newnum >= 0 && newnum < ge->max_edicts) {
                base = &sv.baselines[newnum];
            }
            if (debug_entities && newent) {
                fprintf(stderr,
                        "SV entity add frame=%u ent=%d model=%u class=%u origin=(%.1f %.1f %.1f) radius=%.1f\n",
                        (unsigned)sv.framenum,
                        newnum,
                        (unsigned)newent->model,
                        (unsigned)newent->class_id,
                        newent->origin.x,
                        newent->origin.y,
                        newent->origin.z,
                        newent->radius);
                added++;
            }
            MSG_WriteDeltaEntity(msg, base, newent, false);
            newindex++;
            continue;
        }
        if (newnum > oldnum) { // the old entity isn't present in the new message
            if (debug_entities && oldent) {
                fprintf(stderr,
                        "SV entity remove frame=%u ent=%d model=%u class=%u origin=(%.1f %.1f %.1f)\n",
                        (unsigned)sv.framenum,
                        oldnum,
                        (unsigned)oldent->model,
                        (unsigned)oldent->class_id,
                        oldent->origin.x,
                        oldent->origin.y,
                        oldent->origin.z);
                removed++;
            }
            MSG_WriteLong(msg, 1u << U_REMOVE);
            MSG_WriteShort(msg, oldnum);
            oldindex++;
            continue;
        }
    }
    MSG_WriteEntityBits(msg, 0, 0);    // end of packetentities
    if (debug_entities > 1 && (added || removed || changed)) {
        fprintf(stderr,
                "SV entity summary frame=%u add=%d remove=%d change=%d to=%u from=%d\n",
                (unsigned)sv.framenum,
                added,
                removed,
                changed,
                (unsigned)to->num_entities,
                from_num_entities);
    }
}

void SV_WritePlayerstateToClient(LPCCLIENTFRAME from, LPCCLIENTFRAME to, LPSIZEBUF msg) {
    LPCPLAYER ps = &to->ps;
    LPCPLAYER ops = NULL;
    PLAYER dummy;
    if (!from) {
        memset(&dummy, 0, sizeof(dummy));
        ops = &dummy;
    } else {
        ops = &from->ps;
    }
    MSG_WriteByte(msg, svc_playerinfo);
    MSG_WriteDeltaPlayerState(msg, ops, ps);
}

/* Write the full frame packet (svc_frame header + player state + entity list)
 * to the client's outgoing channel and record the sent frame number so the
 * next call can compute the correct delta. */
void SV_WriteFrameToClient(LPCLIENT client) {
    LPCLIENTFRAME frame = &client->frames[sv.framenum & UPDATE_MASK];
    LPCLIENTFRAME oldframe = client->lastframe == (DWORD)-1
        ? NULL
        : &client->frames[client->lastframe & UPDATE_MASK];
    DWORD start_size = client->netchan.message.cursize;

    MSG_WriteByte(&client->netchan.message, svc_frame);
    MSG_WriteLong(&client->netchan.message, sv.framenum);
    MSG_WriteLong(&client->netchan.message, sv.time);
    MSG_WriteLong(&client->netchan.message, client->lastframe);

    SV_WritePlayerstateToClient(oldframe, frame, &client->netchan.message);
    SV_EmitPacketEntities(oldframe, frame, &client->netchan.message);

    client->lastframe = sv.framenum;

    if (client->netchan.message.overflowed ||
        client->netchan.message.cursize + 1024 >= client->netchan.message.maxsize) {
        fprintf(stderr,
                "SV_WriteFrameToClient: frame=%u entities=%u old_entities=%u bytes=%u start=%u max=%u overflow=%d\n",
                (unsigned)sv.framenum,
                (unsigned)frame->num_entities,
                oldframe ? (unsigned)oldframe->num_entities : 0,
                (unsigned)client->netchan.message.cursize,
                (unsigned)start_size,
                (unsigned)client->netchan.message.maxsize,
                client->netchan.message.overflowed ? 1 : 0);
    }
    Netchan_Transmit(NS_SERVER, &client->netchan);
}
