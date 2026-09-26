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
#define SNAPSHOT_NEAR_ENTITY_DISTANCE 256 // world units; nearby world presentation outranks distant contacts under saturation
#define HIGH_NUMBER 9999
#define OWNED_ENTITY_SCORE_BIAS 1000000000.0f

typedef struct {
    edict_t *edict;
    float score;
    bool priority;
    bool near_world;
} visibleEntityCandidate_t;

/* Determine whether a client should receive updates for the given entity.
 * Always true for the client's own player-owned entities; otherwise based
 * on a simple distance check against the client's camera position. */
static bool SV_CanClientSeeEntity(client_t const *client, edict_t const *edict) {
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
    if (fabs(edict->s.origin.x - clent->client->ps.vieworigin.x) > VISUAL_DISTANCE)
        return false;
    if (fabs(edict->s.origin.y - clent->client->ps.vieworigin.y) > VISUAL_DISTANCE)
        return false;
    return true;
#endif
}

static float SV_ClientEntityVisibilityScore(client_t const *client, edict_t const *edict) {
#ifdef WOW
    (void)client;
    (void)edict;
    return 0.0f;
#else
    edict_t *clent = client->edict;
    float dx = edict->s.origin.x - clent->client->ps.vieworigin.x;
    float dy = edict->s.origin.y - clent->client->ps.vieworigin.y;
    float score = dx * dx + dy * dy;

    if (edict->s.player == clent->client->ps.number) {
        score -= OWNED_ENTITY_SCORE_BIAS;
    }
    return score;
#endif
}

static int SV_CompareCandidateByNumber(void const *a, void const *b) {
    visibleEntityCandidate_t const *ea = a;
    visibleEntityCandidate_t const *eb = b;
    return ea->edict->s.number - eb->edict->s.number;
}

/* Candidate heap root is the lowest-priority, farthest retained entity. */
static bool SV_CandidateWorse(visibleEntityCandidate_t const *a, visibleEntityCandidate_t const *b) {
    if (a->near_world != b->near_world) return !a->near_world;
    if (a->priority != b->priority) return !a->priority;
    return a->score > b->score;
}

/* Keep the worst retained candidate at the root so overflow replacement is logarithmic. */
static void SV_AddVisibleEntityCandidate(visibleEntityCandidate_t *candidates,
                                         int *num_candidates,
                                         edict_t *edict,
                                         float score,
                                         bool priority,
                                         bool near_world)
{
    visibleEntityCandidate_t candidate = { edict, score, priority, near_world };
    if (*num_candidates < MAX_PACKET_ENTITIES) {
        int index = (*num_candidates)++;
        candidates[index] = candidate;
        while (index > 0) {
            int parent = (index - 1) / 2;
            if (!SV_CandidateWorse(&candidates[index], &candidates[parent]))
                break;
            visibleEntityCandidate_t tmp = candidates[parent];
            candidates[parent] = candidates[index]; candidates[index] = tmp;
            index = parent;
        }
        return;
    }
    if (!SV_CandidateWorse(&candidates[0], &candidate))
        return;
    candidates[0] = candidate;
    for (int index = 0;;) {
        int left = index * 2 + 1, right = left + 1, worst = index;
        if (left < *num_candidates && SV_CandidateWorse(&candidates[left], &candidates[worst])) worst = left;
        if (right < *num_candidates && SV_CandidateWorse(&candidates[right], &candidates[worst])) worst = right;
        if (worst == index)
            break;
        visibleEntityCandidate_t tmp = candidates[index];
        candidates[index] = candidates[worst]; candidates[worst] = tmp;
        index = worst;
    }
}

entityState_t *SV_NextClientEntity(void) {
    int index = svs.next_client_entities++ % svs.num_client_entities;
    return &svs.client_entities[index];
}

/* Populate a client frame snapshot with all entities visible to the client.
 * The snapshot records the player state and a list of entity states that will
 * be delta-encoded and sent by SV_WriteFrameToClient. */
void SV_BuildClientFrame(client_t *client) {
    edict_t *clent = client->edict;
    clientFrame_t *frame = &client->frames[sv.framenum & UPDATE_MASK];
    /* Keep the per-client, per-frame candidate workspace off the stack. */
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
#ifdef WC3_DEBUG_MINING
        bool const mining_entity = edict->s.class_id == MAKEFOURCC('n','g','o','l');
#endif
        if (!edict->inuse)
            continue;
        if (edict->svflags & SVF_NOCLIENT) {
#ifdef WC3_DEBUG_MINING
            if (mining_entity) fprintf(stderr, "WC3_MINING snapshot-skip client=%d ent=%d reason=noclient "
                                           "hidden=%d model=%d flags=%u\n", client->edict->client->ps.number, index,
                                           !!(edict->s.renderfx & RF_HIDDEN), !!edict->s.model,
                                           (unsigned)edict->s.flags);
#endif
            continue;
        }
        /* Owner-only entities are private snapshot state, unlike ordinary owned units. */
        if ((edict->svflags & SVF_OWNER_ONLY) && edict->s.player != clent->client->ps.number)
            continue;
        if (!edict->s.model && !edict->s.sound && !edict->s.event) {
#ifdef WC3_DEBUG_MINING
            if (mining_entity) fprintf(stderr, "WC3_MINING snapshot-skip client=%d ent=%d reason=no-presentation "
                                           "hidden=%d noclient=%d\n", client->edict->client->ps.number, index,
                                           !!(edict->s.renderfx & RF_HIDDEN),
                                           !!(edict->svflags & SVF_NOCLIENT));
#endif
            continue;
        }
        if (!SV_CanClientSeeEntity(client, edict) && index > ge->max_clients) {
#ifdef WC3_DEBUG_MINING
            if (mining_entity) fprintf(stderr, "WC3_MINING snapshot-skip client=%d ent=%d reason=out-of-view "
                                           "origin=(%.1f,%.1f) camera=(%.1f,%.1f) hidden=%d model=%d\n",
                                           client->edict->client->ps.number, index, edict->s.origin.x, edict->s.origin.y,
                                           clent->client->ps.vieworigin.x, clent->client->ps.vieworigin.y,
                                           !!(edict->s.renderfx & RF_HIDDEN), !!edict->s.model);
#endif
            continue;
        }
#ifdef WC3_DEBUG_MINING
        if (mining_entity) fprintf(stderr, "WC3_MINING snapshot-add client=%d ent=%d hidden=%d "
                                       "noclient=%d model=%d origin=(%.1f,%.1f)\n", client->edict->client->ps.number, index,
                                       !!(edict->s.renderfx & RF_HIDDEN),
                                       !!(edict->svflags & SVF_NOCLIENT), !!edict->s.model,
                                       edict->s.origin.x, edict->s.origin.y);
#endif
        {
            bool const priority = ge->IsSnapshotPriorityEntity &&
                ge->IsSnapshotPriorityEntity(clent->client->ps.number, edict);
            bool const owned = edict->s.player == clent->client->ps.number;
            bool const near_world = owned || (!priority &&
                fabs(edict->s.origin.x - clent->client->ps.vieworigin.x) <= SNAPSHOT_NEAR_ENTITY_DISTANCE &&
                fabs(edict->s.origin.y - clent->client->ps.vieworigin.y) <= SNAPSHOT_NEAR_ENTITY_DISTANCE);
            SV_AddVisibleEntityCandidate(candidates, &num_candidates, edict,
                                         SV_ClientEntityVisibilityScore(client, edict), priority, near_world);
        }
    }

    qsort(candidates,
          (size_t)num_candidates,
          sizeof(candidates[0]),
          SV_CompareCandidateByNumber);

    for (int index = 0; index < num_candidates; index++) {
        edict_t *edict = candidates[index].edict;
        entityState_t *state = SV_NextClientEntity();
        *state = edict->s;
        ge->CustomizeEntity(clent->client->ps.number, edict, state);
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
void SV_EmitPacketEntities(clientFrame_t const *from, clientFrame_t const *to, sizeBuf_t *msg) {
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
        entityState_t *newent = NULL;
        entityState_t *oldent = NULL;
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
            entityState_t const *base = &nullstate;
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

void SV_WritePlayerstateToClient(clientFrame_t const *from, clientFrame_t const *to, sizeBuf_t *msg) {
    player_t const *ps = &to->ps;
    player_t const *ops = NULL;
    player_t dummy;
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
void SV_WriteFrameToClient(client_t *client) {
    clientFrame_t *frame = &client->frames[sv.framenum & UPDATE_MASK];
    clientFrame_t *oldframe = client->lastframe == (uint32_t)-1
        ? NULL
        : &client->frames[client->lastframe & UPDATE_MASK];
    uint32_t start_size = client->netchan.message.cursize;

    MSG_WriteByte(&client->netchan.message, svc_frame);
    MSG_WriteLong(&client->netchan.message, sv.framenum);
    MSG_WriteLong(&client->netchan.message, sv.time);
    MSG_WriteLong(&client->netchan.message, client->lastframe);
    {
        uint8_t data[MAX_GAME_DATAGRAM_SIZE];
        uint32_t size = ge->WriteClientDatagram(client->edict, data, sizeof(data));
        if (size > sizeof(data)) {
            fprintf(stderr, "SV_WriteFrameToClient: game datagram too large (%u)\n", (unsigned)size);
            size = 0;
        }
        SZ_Write(&client->netchan.message, data, size);
    }

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
