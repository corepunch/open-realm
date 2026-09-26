#include "server.h"

#define AREA_DEPTH 6
#define AREA_NODES 128

#define STRUCT_FROM_LINK(l,t,m) ((t *)((uint8_t *)l - (long long)&(((t *)0)->m)))
#define EDICT_FROM_AREA(l) STRUCT_FROM_LINK(l,edict_t,area)
#define GET_AXIS(vec, axis) (*((float const *)(vec)+axis))
#define SET_AXIS(vec, axis, value) (*((float *)(vec)+axis))=value

KNOWN_AS(areanode_s, areaNode_t);

struct areanode_s {
    uint32_t axis;  // -1 = leaf node
    uint32_t depth; // for debug
    box2_t bounds;
    float dist;
    struct areanode_s *children[2];
//    link_t trigger_edicts;
    link_t solid_edicts;
};

static areaNode_t sv_areanodes[AREA_NODES];
static uint32_t sv_numareanodes;

void ClearLink (link_t * l) {
    l->prev = l->next = l;
}

void RemoveLink (link_t * l) {
    l->next->prev = l->prev;
    l->prev->next = l->next;
}

void InsertLinkBefore (link_t * l, link_t * before) {
    l->next = before;
    l->prev = before->prev;
    l->prev->next = l;
    l->next->prev = l;
}

areaNode_t * SV_CreateAreaNode(uint32_t depth, vector2_t const * mins, vector2_t const * maxs) {
    areaNode_t * anode = &sv_areanodes[sv_numareanodes++];
    vector2_t size = Vector2_sub(maxs, mins);
    vector2_t mins1 = *mins, mins2 = *mins, maxs1 = *maxs, maxs2 = *maxs;

    ClearLink (&anode->solid_edicts);
 
    anode->bounds = MAKE(box2_t, *mins, *maxs);
    anode->depth = depth;

    if (depth == AREA_DEPTH) {
        anode->axis = -1;
        anode->children[0] = anode->children[1] = NULL;
        return anode;
    }
        
    anode->axis = size.x < size.y;
    anode->dist = 0.5 * (GET_AXIS(maxs, anode->axis) + GET_AXIS(mins, anode->axis));
    
    SET_AXIS(&maxs1, anode->axis, anode->dist);
    SET_AXIS(&mins2, anode->axis, anode->dist);

    anode->children[0] = SV_CreateAreaNode(depth+1, &mins2, &maxs2);
    anode->children[1] = SV_CreateAreaNode(depth+1, &mins1, &maxs1);
    
    return anode;
}

void SV_ClearWorld(void) {
    memset(sv_areanodes, 0, sizeof(sv_areanodes));
    sv_numareanodes = 0;
    box2_t bounds = ge->GetWorldBounds();
    SV_CreateAreaNode(0, &bounds.min, &bounds.max);
}

void SV_UnlinkEntity(edict_t * ent) {
    if (!ent->area.prev)
        return;        // not linked in anywhere
    RemoveLink(&ent->area);
    ent->area.prev = ent->area.next = NULL;
}

void SV_LinkEntity(edict_t * ent) {
    SV_UnlinkEntity(ent);
    
    if (ent == ge->edicts)
        return; // don't add the world

    if (!ent->inuse)
        return;

    vector2_t const size = { ent->collision, ent->collision };
    vector2_t const eps = { 1, 1 };
    
    ent->areanum = 0;
    ent->bounds.min = Vector2_sub(&ent->s.origin2, &size);
    ent->bounds.max = Vector2_add(&ent->s.origin2, &size);

    // because movement is clipped an epsilon away from an actual edge,
    // we must fully check even when bounding boxes don't quite touch
    ent->bounds.min = Vector2_sub(&ent->bounds.min, &eps);
    ent->bounds.max = Vector2_add(&ent->bounds.max, &eps);

    areaNode_t * node = sv_areanodes;
    while (1) {
        if (node->axis == -1)
            break;
        if (GET_AXIS(&ent->bounds.min, node->axis) > node->dist)
            node = node->children[0];
        else if (GET_AXIS(&ent->bounds.max, node->axis) < node->dist)
            node = node->children[1];
        else
            break; // crosses the node
    }
    InsertLinkBefore(&ent->area, &node->solid_edicts);
    ent->areabounds = node->bounds;
}

typedef struct {
    box2_t bounds;
    edict_t * *list;
    uint32_t maxcount;
    uint32_t count;
    bool (*pred)(edict_t const *);
} areaworker_t;

void SV_AreaEdicts_r(areaNode_t const * node, areaworker_t *worker) {
    link_t const * start = &node->solid_edicts;
    
    for (link_t const * l = start->next; l != start; l = l->next) {
        edict_t * check = EDICT_FROM_AREA(l);

        if (   check->bounds.min.x > worker->bounds.max.x
            || check->bounds.min.y > worker->bounds.max.y
            || check->bounds.max.x < worker->bounds.min.x
            || check->bounds.max.y < worker->bounds.min.y)
            continue; // not touching

        if (worker->count == worker->maxcount) {
            fprintf(stdout, "SV_AreaEdicts: MAXCOUNT\n");
            return;
        }

        if (!worker->pred || worker->pred(check)) {
            worker->list[worker->count++] = check;
        }
    }

    if (node->axis == -1)
        return; // terminal node

    // recurse down both sides
    if (GET_AXIS(&worker->bounds.max, node->axis) > node->dist)
        SV_AreaEdicts_r(node->children[0], worker);
    
    if (GET_AXIS(&worker->bounds.min, node->axis) < node->dist)
        SV_AreaEdicts_r(node->children[1], worker);
}

uint32_t SV_AreaEdicts(box2_t const * area, edict_t * *list, uint32_t maxcount, bool (*pred)(edict_t const *)) {
    areaworker_t w = {
        .bounds = *area,
        .list = list,
        .count = 0,
        .maxcount = maxcount,
        .pred = pred,
    };
    SV_AreaEdicts_r(sv_areanodes, &w);
    return w.count;
}
