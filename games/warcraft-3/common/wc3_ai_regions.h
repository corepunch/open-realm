#ifndef BZ_WC3_AI_REGIONS_H
#define BZ_WC3_AI_REGIONS_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "wc3_math.h"
#include "wc3_pathing_random.h"

#define WC3_AI_REGION_SIDE 256u
#define WC3_AI_REGION_LIMIT 2048u
#define WC3_AI_REGION_NONE UINT32_MAX

typedef struct {
    uint32_t x,y,processed,source_mark,near_mark,visited,edge_capacity;
    uint32_t edges[4],weight_capacity,weights[4];
} wc3AiRegionNode_t;
typedef struct {
    uint32_t width,height,count,eligible_count,near_count;
    uint8_t grid[65536],temporary[65536];
    wc3AiRegionNode_t nodes[WC3_AI_REGION_LIMIT];
    uint32_t eligible[WC3_AI_REGION_LIMIT],near[WC3_AI_REGION_LIMIT];
} wc3AiRegions_t;

/* Original read-only masks cfcca8/cfcba0/cfcdb0/cfca98, in visit order. */
static uint8_t const wc3_ai_region_patterns[4][257]={
    "................................................................@@..@@..........................@@..@@..@@.@@@.@................................................................................@@..@@..........@@..@@@@........@@..@@..@@.@@@..@@..@@@.@@..@...",
    "................@@..@@@@.......@................@@..@@@@.......@................................................................................@@..@@@@.......@................@@..@@@@........................@@..@@@@........................@@..@@@.@@..@...",
    "..@@..@@...@...@......@@.......@..@@..@@...@...@......@@.......@...........................................@...@..................@@..@@...@...@......@@.......@..@@..@@...@...@......@@..............................@@...................@..........@.........",
    "........@@.@@@.@...............@........@@.@@@.@...............@........................................@@.@@@.@........................@@.@@@.@...............@........@@.@@@.@........................................................@@.@@@..........@@..@...",
};
static uint8_t const wc3_ai_region_neighbor_counts[256]={
    1,1,1,1,1,2,1,1,1,1,2,1,2,2,2,1,1,2,2,2,1,2,1,1,2,2,3,2,2,2,2,1,
    1,2,2,2,2,3,2,2,1,1,2,1,2,2,2,1,2,3,3,3,2,3,2,2,2,2,3,2,2,2,2,1,
    1,2,2,2,2,3,2,2,2,2,3,2,3,3,3,2,2,3,3,3,2,3,2,2,3,3,4,3,3,3,3,2,
    1,2,2,2,2,3,2,2,1,1,2,1,2,2,2,1,2,3,3,3,2,3,2,2,2,2,3,2,2,2,2,1,
    1,2,2,2,2,3,2,2,2,2,3,2,3,3,3,2,1,2,2,2,1,2,1,1,2,2,3,2,2,2,2,1,
    2,3,3,3,3,4,3,3,2,2,3,2,3,3,3,2,2,3,3,3,2,3,2,2,2,2,3,2,2,2,2,1,
    1,2,2,2,2,3,2,2,2,2,3,2,3,3,3,2,1,2,2,2,1,2,1,1,2,2,3,2,2,2,2,1,
    1,2,2,2,2,3,2,2,1,1,2,1,2,2,2,1,1,2,2,2,1,2,1,1,1,1,2,1,1,1,1,0,
};

static inline bool wc3_ai_region_free(uint8_t c) {return c=='.'||c=='+'||c=='X';}
static inline uint32_t wc3_ai_region_mask(wc3AiRegions_t const *g,uint8_t const *cells,int x,int y) {
    static int8_t const dx[8]={-1,0,1,-1,1,-1,0,1},dy[8]={1,1,1,0,0,-1,-1,-1};
    uint32_t mask=0;
    for(uint32_t i=0;i<8;i++) {
        int a=x+dx[i],b=y+dy[i];
        if(a<0||b<0||a>=(int)g->width||b>=(int)g->height||!wc3_ai_region_free(cells[b*256+a]))mask|=1u<<i;
    }
    return mask;
}

static inline void wc3_ai_region_edge(wc3AiRegions_t *g,uint32_t source,uint32_t target,uint32_t cost) {
    wc3AiRegionNode_t *n=g->nodes+source;
    for(uint32_t i=0;i<4;i++)if(n->edges[i]==target){if(cost<n->weights[i])n->weights[i]=cost;return;}
    for(uint32_t i=0;i<4;i++)if(n->edges[i]==WC3_AI_REGION_NONE){n->edges[i]=target;n->weights[i]=cost;return;}
}

/*9c0d20's exact DFS order, using the completed thinning buffer as a byte
 * stack. Each dot is consumed once; map size bounds the depth independently
 * of the host call stack. A successful edge unwinds the whole traversal. */
static inline bool wc3_ai_region_walk(wc3AiRegions_t *g,uint32_t source,uint32_t cost,int x,int y) {
    static int8_t const dx[8]={1,0,0,-1,1,1,-1,-1},dy[8]={0,1,-1,0,1,-1,1,-1};
    uint32_t depth=0;g->temporary[0]=0;
    for(;;) {
        if(!(g->temporary[depth]&0x80)) {
            if(x>=0&&y>=0&&x<(int)g->width&&y<(int)g->height) {
                uint8_t *c=g->grid+y*256+x;
                if(*c=='.'){*c='+';g->temporary[depth]=0x80;}
                else if(*c=='X') {
                    uint32_t target=0;
                    while(target<g->count&&(g->nodes[target].x!=(uint32_t)x||g->nodes[target].y!=(uint32_t)y))target++;
                    if(target==g->count)target=0; /* Literal native lookup fallback. */
                    if(target!=source) {
                        wc3_ai_region_edge(g,source,target,cost);
                        if(depth+1>=2)wc3_ai_region_edge(g,target,source,cost);
                        return true;
                    }
                }
            }
        }
        uint32_t child=g->temporary[depth]&15u;
        if((g->temporary[depth]&0x80)&&child<8) {
            g->temporary[depth]++;
            x+=dx[child];y+=dy[child];cost+=child<4?169u:239u;
            depth++;g->temporary[depth]=0;continue;
        }
        if(!depth)return false;
        depth--;child=(g->temporary[depth]&15u)-1;
        x-=dx[child];y-=dy[child];cost-=child<4?169u:239u;
    }
}

static inline void wc3_ai_region_trace(wc3AiRegions_t *g,uint32_t index) {
    wc3AiRegionNode_t *n=g->nodes+index;int x=n->x,y=n->y;n->processed=1;
    bool down=wc3_ai_region_walk(g,index,169,x,y+1),up=wc3_ai_region_walk(g,index,169,x,y-1);
    bool left=wc3_ai_region_walk(g,index,169,x-1,y),right=wc3_ai_region_walk(g,index,169,x+1,y);
    if(!down){if(!right)wc3_ai_region_walk(g,index,239,x+1,y+1);if(!left)wc3_ai_region_walk(g,index,239,x-1,y+1);}
    if(!up){if(!right)wc3_ai_region_walk(g,index,239,x+1,y-1);if(!left)wc3_ai_region_walk(g,index,239,x-1,y-1);}
}

static inline bool wc3_ai_regions_build(wc3AiRegions_t *g,uint32_t width,uint32_t height,uint8_t const *classes) {
    if(!g||!classes||width>256||height>256)return false;
    memset(g,0,sizeof(*g));g->width=width;g->height=height;
    for(uint32_t y=0;y<height;y++)for(uint32_t x=0;x<width;x++) {
        uint8_t c=classes[y*width+x];g->grid[y*256+x]=c==0?'.':c==1?'#':'?';
    }
    bool changed;
    do {
        changed=false;
        for(uint32_t p=0;p<4;p++) {
            memcpy(g->temporary,g->grid,65536);
            for(uint32_t y=0;y<height;y++)for(uint32_t x=0;x<width;x++) {
                uint32_t i=y*256+x;
                if(g->temporary[i]=='.'&&wc3_ai_region_patterns[p][wc3_ai_region_mask(g,g->temporary,x,y)]=='@') {
                    g->grid[i]='@';changed=true;
                }
            }
        }
    }while(changed);
    for(uint32_t y=0;y<height;y++)for(uint32_t x=0;x<width;x++)
        if(!(wc3_ai_region_mask(g,g->grid,x,y)&0x5a))g->grid[y*256+x]='@';
    memcpy(g->temporary,g->grid,65536);
    for(uint32_t y=0;y<height;y++)for(uint32_t x=0;x<width;x++)
        if(g->temporary[y*256+x]=='.'&&wc3_ai_region_neighbor_counts[wc3_ai_region_mask(g,g->temporary,x,y)]>2)g->grid[y*256+x]='X';
    for(uint32_t y=0;y<height;y++)for(uint32_t x=0;x<width;x++)if(g->grid[y*256+x]=='X') {
        if(g->count==WC3_AI_REGION_LIMIT)return false;
        wc3AiRegionNode_t *n=g->nodes+g->count++;
        *n=(wc3AiRegionNode_t){.x=x,.y=y,.edge_capacity=4,.weight_capacity=4};
        for(uint32_t i=0;i<4;i++)n->edges[i]=n->weights[i]=WC3_AI_REGION_NONE;
    }
    for(uint32_t i=0;i<g->count;i++)if(!g->nodes[i].processed)wc3_ai_region_trace(g,i);
    for(uint32_t i=0;i<g->count;i++) {
        uint32_t k=i%62;g->grid[g->nodes[i].y*256+g->nodes[i].x]=(uint8_t)(k<26?k+65:k<52?k+71:k-4);
    }
    return true;
}

/* Native inline exponent scaling after Math_Multiply; valid cell scales are
 * normal powers of two. Keep its zero/subnormal word behavior explicit. */
static inline float wc3_ai_region_world(uint32_t cell,float scale,float origin) {
    uint32_t word=wc3_mul_bits(wc3_from_int(cell),wc3_float_bits(scale));
    if(word&0x7f800000u)word+=0x02800000u;
    return wc3_add(wc3_float(word),origin);
}

static inline uint32_t wc3_ai_region_nearest(wc3AiRegions_t const *g,float const point[2],float const origin[2],float scale) {
    float best=409600;uint32_t result=WC3_AI_REGION_NONE;
    for(uint32_t i=0;i<g->count;i++) {
        float x=wc3_sub(wc3_ai_region_world(g->nodes[i].x,scale,origin[0]),point[0]);
        float y=wc3_sub(wc3_ai_region_world(g->nodes[i].y,scale,origin[1]),point[1]);
        float distance=wc3_add(wc3_mul(x,x),wc3_mul(y,y));
        if(distance<=best){best=distance;result=i;}
    }
    return result;
}

static inline void wc3_ai_regions_mark(wc3AiRegions_t *g,float const point[2],float const origin[2],float scale) {
    uint32_t i=wc3_ai_region_nearest(g,point,origin,scale);
    if(i!=WC3_AI_REGION_NONE)g->nodes[i].source_mark=g->nodes[i].near_mark=1;
}

/* Union of native9b9a20's positive-weight walks below2501. Dijkstra avoids
 * revisiting longer cycles and preserves the exact final boolean marks. */
static inline void wc3_ai_regions_expand_marks(wc3AiRegions_t *g) {
    uint32_t distance[WC3_AI_REGION_LIMIT];uint8_t done[WC3_AI_REGION_LIMIT]={0};
    for(uint32_t i=0;i<g->count;i++)distance[i]=g->nodes[i].source_mark?0u:2501u;
    for(uint32_t step=0;step<g->count;step++) {
        uint32_t best=2501,index=WC3_AI_REGION_NONE;
        for(uint32_t i=0;i<g->count;i++)if(!done[i]&&distance[i]<best){best=distance[i];index=i;}
        if(index==WC3_AI_REGION_NONE)break;
        done[index]=1;g->nodes[index].near_mark=1;
        wc3AiRegionNode_t const *n=g->nodes+index;
        for(uint32_t e=0;e<4;e++)if(n->edges[e]!=WC3_AI_REGION_NONE) {
            uint32_t d=best+n->weights[e],to=n->edges[e];if(d<distance[to])distance[to]=d;
        }
    }
}

/* Preserve native DFS's visitation words as well as its answer. The target
 * may be marked; every intermediate must be unmarked. Start==target still
 * requires an edge path, matching9bd7b0's explicit first-neighbor loop. */
static inline bool wc3_ai_region_reachable(wc3AiRegions_t *g,uint32_t source,uint32_t target) {
    typedef struct {uint32_t node,child;} visit_t;
    visit_t stack[WC3_AI_REGION_LIMIT];
    for(uint32_t i=0;i<g->count;i++)g->nodes[i].visited=0;
    uint32_t depth=0;stack[0]=(visit_t){source,0};
    for(;;) {
        if(stack[depth].child==4){if(!depth)return false;depth--;continue;}
        uint32_t next=g->nodes[stack[depth].node].edges[stack[depth].child++];
        if(next==target)return true;
        if(next==WC3_AI_REGION_NONE||g->nodes[next].visited||g->nodes[next].near_mark)continue;
        g->nodes[next].visited=1;depth++;stack[depth]=(visit_t){next,0};
    }
}

static inline void wc3_ai_regions_filter(wc3AiRegions_t *g,uint32_t source,float const point[2],float const origin[2],float scale) {
    g->eligible_count=g->near_count=0;
    if(source==WC3_AI_REGION_NONE)return;
    for(uint32_t i=0;i<g->count;i++)if(g->nodes[i].near_mark&&wc3_ai_region_reachable(g,i,source)) {
        g->eligible[g->eligible_count++]=i;
        float x=wc3_sub(wc3_ai_region_world(g->nodes[i].x,scale,origin[0]),point[0]);
        float y=wc3_sub(wc3_ai_region_world(g->nodes[i].y,scale,origin[1]),point[1]);
        if(wc3_add(wc3_mul(x,x),wc3_mul(y,y))<=25000000)g->near[g->near_count++]=i;
    }
}

static inline bool wc3_ai_regions_roll(wc3AiRegions_t const *g,wc3Random_t *random,float const origin[2],float scale,float point[2]) {
    uint32_t count=g->near_count?g->near_count:g->eligible_count;if(!count)return false;
    uint32_t const *list=g->near_count?g->near:g->eligible;
    wc3AiRegionNode_t const *n=g->nodes+list[wc3_random_range(random,count)];
    point[0]=wc3_ai_region_world(n->x,scale,origin[0]);point[1]=wc3_ai_region_world(n->y,scale,origin[1]);return true;
}

#endif
