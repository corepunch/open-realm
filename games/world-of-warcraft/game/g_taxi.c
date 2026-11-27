#include "g_taxi.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Read WDBC without trusting multiplication or NUL termination in its input. */
typedef struct { const uint8_t *data, *strings; uint32_t count, fields, stride, string_size; } taxiDbc_t;
static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static float readfloat(const uint8_t *p) { uint32_t bits = read32(p); float f; memcpy(&f, &bits, 4); return f; }
static bool dbc(taxiDbc_t *d, const uint8_t *p, size_t n, uint32_t min_fields) {
    size_t bytes;
    if (!p || n < 20 || memcmp(p, "WDBC", 4)) return false;
    d->count = read32(p+4); d->fields = read32(p+8); d->stride = read32(p+12); d->string_size = read32(p+16);
    if (d->fields < min_fields || d->stride < min_fields * 4 || d->count > (n-20)/d->stride) return false;
    bytes = (size_t)d->count * d->stride;
    if (d->string_size > n-20-bytes) return false;
    d->data = p+20; d->strings = d->data+bytes;
    return true;
}
static bool validpos(float x, float y, float z) { return isfinite(x) && isfinite(y) && isfinite(z); }
void WowTaxi_Free(wowTaxiGraph_t *g) {
    if (!g) return;
    free(g->nodes); free(g->paths); free(g->points); memset(g, 0, sizeof(*g));
}
const wowTaxiNode_t *WowTaxi_FindNode(const wowTaxiGraph_t *g, uint32_t id) {
    size_t i; if (!g || !id) return NULL;
    for (i=0; i<g->node_count; ++i) if (g->nodes[i].id == id) return &g->nodes[i];
    return NULL;
}
static int pointcmp(const void *a, const void *b) {
    const wowTaxiPoint_t *x=a, *y=b;
    if (x->path != y->path) return x->path < y->path ? -1 : 1;
    return x->index == y->index ? 0 : x->index < y->index ? -1 : 1;
}
bool WowTaxi_Load(wowTaxiGraph_t *g, const uint8_t *nodes, size_t ns, const uint8_t *paths, size_t ps, const uint8_t *points, size_t qs) {
    taxiDbc_t n, p, q; wowTaxiGraph_t tmp = {0}; size_t i, j;
    if (!g || !dbc(&n,nodes,ns,6) || !dbc(&p,paths,ps,4) || !dbc(&q,points,qs,9)) return false;
    if (!(tmp.nodes=calloc(n.count ? n.count : 1,sizeof(*tmp.nodes))) ||
        !(tmp.paths=calloc(p.count ? p.count : 1,sizeof(*tmp.paths))) ||
        !(tmp.points=calloc(q.count ? q.count : 1,sizeof(*tmp.points)))) goto fail;
    for (i=0;i<n.count;i++) {
        const uint8_t *r=n.data+i*n.stride; uint32_t off=read32(r+20); wowTaxiNode_t *v=&tmp.nodes[tmp.node_count]; const uint8_t *end;
        v->id=read32(r); v->map=read32(r+4); v->x=readfloat(r+8); v->y=readfloat(r+12); v->z=readfloat(r+16);
        if (!v->id || !validpos(v->x,v->y,v->z) || WowTaxi_FindNode(&tmp,v->id) || off>=n.string_size) continue;
        end=memchr(n.strings+off,0,n.string_size-off); if (!end) continue;
        { size_t len=(size_t)(end-(n.strings+off)); if (len>=sizeof(v->name)) len=sizeof(v->name)-1; memcpy(v->name,n.strings+off,len); v->name[len]=0; }
        tmp.node_count++;
    }
    for (i=0;i<p.count;i++) {
        const uint8_t *r=p.data+i*p.stride; wowTaxiPath_t *v=&tmp.paths[tmp.path_count];
        v->id=read32(r); v->from=read32(r+4); v->to=read32(r+8); v->price=read32(r+12);
        if (!v->id || !WowTaxi_FindNode(&tmp,v->from) || !WowTaxi_FindNode(&tmp,v->to)) continue;
        for (j=0;j<tmp.path_count;j++) if (tmp.paths[j].id==v->id) break;
        if (j==tmp.path_count) tmp.path_count++;
    }
    for (i=0;i<q.count;i++) {
        const uint8_t *r=q.data+i*q.stride; wowTaxiPoint_t *v=&tmp.points[tmp.point_count]; bool found=false;
        v->path=read32(r+4); v->index=read32(r+8); v->map=read32(r+12); v->x=readfloat(r+16); v->y=readfloat(r+20); v->z=readfloat(r+24); v->flags=read32(r+28); v->delay=read32(r+32);
        for (j=0;j<tmp.path_count;j++) if (tmp.paths[j].id==v->path) { found=true; break; }
        if (!found || !validpos(v->x,v->y,v->z)) continue;
        tmp.point_count++;
    }
    qsort(tmp.points,tmp.point_count,sizeof(*tmp.points),pointcmp);
    for (i=1;i<tmp.point_count;i++) if (tmp.points[i].path==tmp.points[i-1].path && tmp.points[i].index==tmp.points[i-1].index) goto fail;
    for (i = 0; i < tmp.path_count; i++) {
        size_t first = 0;
        while (first < tmp.point_count && tmp.points[first].path < tmp.paths[i].id) first++;
        tmp.paths[i].first_point = first;
        while (first < tmp.point_count && tmp.points[first].path == tmp.paths[i].id) {
            tmp.paths[i].point_count++;
            first++;
        }
    }
    *g=tmp; return true;
fail: WowTaxi_Free(&tmp); return false;
}
static bool taxi_known(const uint8_t *known, size_t size, uint32_t id) {
    return (size_t)(id / 8) < size && (known[id / 8] & (1u << (id % 8)));
}

bool WowTaxi_Route(const wowTaxiGraph_t *g, uint32_t from, uint32_t to,
                   const uint8_t *known, size_t known_size, wowTaxiRoute_t *out) {
    uint64_t *dist;
    size_t *prev;
    bool *done;
    size_t i, j, start = SIZE_MAX, end = SIZE_MAX, cur;
    bool ok = false;
    if (!g || !known || !out || from == to ||
        !taxi_known(known, known_size, from) || !taxi_known(known, known_size, to))
        return false;
    memset(out, 0, sizeof(*out));
    for (i = 0; i < g->node_count; i++) {
        if (g->nodes[i].id == from) start = i;
        if (g->nodes[i].id == to) end = i;
    }
    if (start == SIZE_MAX || end == SIZE_MAX) return false;
    dist = malloc(g->node_count * sizeof(*dist));
    prev = malloc(g->node_count * sizeof(*prev));
    done = calloc(g->node_count, sizeof(*done));
    if (!dist || !prev || !done) goto finish;
    for (i = 0; i < g->node_count; i++) {
        dist[i] = UINT64_MAX;
        prev[i] = SIZE_MAX;
    }
    dist[start] = 0;
    for (i = 0; i < g->node_count; i++) {
        cur = SIZE_MAX;
        for (j = 0; j < g->node_count; j++)
            if (!done[j] && dist[j] != UINT64_MAX &&
                (cur == SIZE_MAX || dist[j] < dist[cur])) cur = j;
        if (cur == SIZE_MAX || cur == end) break;
        done[cur] = true;
        for (j = 0; j < g->path_count; j++) {
            const wowTaxiPath_t *path = &g->paths[j];
            size_t k, first = path->first_point, last;
            if (path->from != g->nodes[cur].id || !taxi_known(known, known_size, path->to))
                continue;
            /* Cross-map flights need an explicit map-transition lifecycle. */
            if (path->point_count < 2 || first > g->point_count ||
                path->point_count > g->point_count - first) continue;
            last = first + path->point_count - 1;
            for (k = first; k <= last; k++)
                if (g->points[k].path != path->id || g->points[k].map != g->nodes[cur].map)
                    break;
            if (k <= last) continue;
            for (k = 0; k < g->node_count; k++)
                if (g->nodes[k].id == path->to) break;
            if (k == g->node_count || g->nodes[k].map != g->nodes[cur].map || done[k])
                continue;
            if (dist[cur] + path->price < dist[k]) {
                dist[k] = dist[cur] + path->price;
                prev[k] = j;
            }
        }
    }
    if (dist[end] == UINT64_MAX) goto finish;
    out->price = dist[end];
    cur = end;
    while (cur != start) {
        size_t edge = prev[cur], parent;
        if (edge == SIZE_MAX || out->count == 128) goto finish;
        out->paths[out->count++] = g->paths[edge].id;
        for (parent = 0; parent < g->node_count; parent++)
            if (g->nodes[parent].id == g->paths[edge].from) break;
        if (parent == g->node_count) goto finish;
        cur = parent;
    }
    for (i = 0; i < out->count / 2; i++) {
        uint32_t path = out->paths[i];
        out->paths[i] = out->paths[out->count - 1 - i];
        out->paths[out->count - 1 - i] = path;
    }
    ok = true;
finish:
    free(dist);
    free(prev);
    free(done);
    if (!ok) memset(out, 0, sizeof(*out));
    return ok;
}
