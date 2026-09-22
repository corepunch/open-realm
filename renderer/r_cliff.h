#ifndef R_CLIFF_H
#define R_CLIFF_H

#include "renderer/r_local.h"

typedef struct {
    VERTEX *vertices;
    DWORD *groups;
    DWORD num_vertices;
    DWORD capacity;
    DWORD current_group;
} rCliffBakeList_t;

typedef struct { int qx, qy, qz; DWORD idx; } rNormalWeldKey_t;

/* Header consumers may only use the pure predicate; unused helpers must not import ri at -O0. */
static inline void R_CliffBakeGrow(rCliffBakeList_t *list, DWORD add) {
    VERTEX *vertices;
    DWORD *groups;
    DWORD capacity;

    if (list->num_vertices + add <= list->capacity)
        return;
    capacity = MAX(1024, list->capacity);
    while (list->num_vertices + add > capacity) {
        capacity *= 2;
    }
    vertices = ri.MemAlloc(capacity * sizeof(*vertices));
    groups = ri.MemAlloc(capacity * sizeof(*groups));
    if (list->vertices) {
        memcpy(vertices, list->vertices, list->num_vertices * sizeof(*vertices));
        memcpy(groups, list->groups, list->num_vertices * sizeof(*groups));
        ri.MemFree(list->vertices);
        ri.MemFree(list->groups);
    }
    list->vertices = vertices;
    list->groups = groups;
    list->capacity = capacity;
}

static inline LPVERTEX R_CliffBakeVertex(rCliffBakeList_t *list) {
    R_CliffBakeGrow(list, 1);
    list->groups[list->num_vertices] = list->current_group;
    return &list->vertices[list->num_vertices++];
}

static inline BOOL R_CliffWeldCompatible(LPCVERTEX a, DWORD a_group, LPCVERTEX b, DWORD b_group, FLOAT z_snap) {
	return a_group != b_group &&
		   (int)roundf(a->position.z / z_snap) == (int)roundf(b->position.z / z_snap) &&
		   Vector3_dot(&a->normal, &b->normal) > 0.0f;
}

static inline int r_cliff_weld_cmp(const void *a, const void *b) {
    rNormalWeldKey_t const *ka = a, *kb = b;
    if (ka->qx != kb->qx) return ka->qx < kb->qx ? -1 : 1;
    if (ka->qy != kb->qy) return ka->qy < kb->qy ? -1 : 1;
    if (ka->qz != kb->qz) return ka->qz < kb->qz ? -1 : 1;
    return 0;
}

/* Weld only coincident, similarly facing cliff vertices; XY-only averaging merged stacked and opposing faces. */
static inline void R_CliffWeldNormals(rCliffBakeList_t *list, FLOAT snap) {
    VERTEX *vertices = list->vertices;
    DWORD n = list->num_vertices;
    rNormalWeldKey_t *keys;
    VECTOR3 *normals;
    DWORD i;

    if (n < 2 || snap <= 0.0f) return;
    keys = ri.MemAlloc(n * sizeof(*keys));
    normals = ri.MemAlloc(n * sizeof(*normals));
    FOR_LOOP(i, n) {
        keys[i].qx = (int)roundf(vertices[i].position.x / snap);
        keys[i].qy = (int)roundf(vertices[i].position.y / snap);
        keys[i].qz = (int)roundf(vertices[i].position.z / 0.001f);
        keys[i].idx = i;
    }
    qsort(keys, n, sizeof(*keys), r_cliff_weld_cmp);
    i = 0;
    while (i < n) {
        DWORD j = i;
        while (j < n && keys[j].qx == keys[i].qx && keys[j].qy == keys[i].qy && keys[j].qz == keys[i].qz)
            j++;
        for (DWORD k = i; k < j; k++) {
            VECTOR3 avg = vertices[keys[k].idx].normal;
            DWORD count = Vector3_len(&avg) > 0.0f;
            for (DWORD l = i; l < j; l++) {
                if (!R_CliffWeldCompatible(&vertices[keys[k].idx], list->groups[keys[k].idx], &vertices[keys[l].idx], list->groups[keys[l].idx], 0.001f)) continue;
                /* Expanded triangles repeat the same authored normal; count each placement/normal once. */
                BOOL duplicate = false;
                for (DWORD m = i; m < l; m++) {
                    VECTOR3 delta = Vector3_sub(&vertices[keys[m].idx].normal, &vertices[keys[l].idx].normal);
                    if (list->groups[keys[m].idx] == list->groups[keys[l].idx] && Vector3_dot(&delta, &delta) < 0.000001f) {
                        duplicate = true; break;
                    }
                }
                if (duplicate) continue;
                avg = Vector3_add(&avg, &vertices[keys[l].idx].normal);
                count++;
            }
            normals[keys[k].idx] = count ? avg : vertices[keys[k].idx].normal;
            if (count) Vector3_normalize(&normals[keys[k].idx]);
        }
        i = j;
    }
    FOR_LOOP(i, n) vertices[i].normal = normals[i];
    ri.MemFree(normals);
    ri.MemFree(keys);
}

#endif
