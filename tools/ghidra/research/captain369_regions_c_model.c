#include <stdlib.h>
#include "games/warcraft-3/common/wc3_ai_regions.h"
void *region_alloc(void){return calloc(1,sizeof(wc3AiRegions_t));}
void region_free(void *p){free(p);}
int region_build(wc3AiRegions_t *g,unsigned w,unsigned h,uint8_t const *cells){return wc3_ai_regions_build(g,w,h,cells);}
unsigned region_count(wc3AiRegions_t const *g){return g->count;}
void region_grid(wc3AiRegions_t const *g,uint8_t *out){memcpy(out,g->grid,65536);}
void region_nodes(wc3AiRegions_t const *g,uint32_t *out){memcpy(out,g->nodes,g->count*sizeof(*g->nodes));}
void region_mark(wc3AiRegions_t *g,float const *p,float const *o,float s){wc3_ai_regions_mark(g,p,o,s);}
void region_expand(wc3AiRegions_t *g){wc3_ai_regions_expand_marks(g);}
unsigned region_nearest(wc3AiRegions_t const *g,float const *p,float const *o,float s){return wc3_ai_region_nearest(g,p,o,s);}
void region_filter(wc3AiRegions_t *g,unsigned source,float const *p,float const *o,float s){wc3_ai_regions_filter(g,source,p,o,s);}
unsigned region_lists(wc3AiRegions_t const *g,uint32_t *out){out[0]=g->eligible_count;out[1]=g->near_count;memcpy(out+2,g->eligible,g->eligible_count*4);memcpy(out+2+g->eligible_count,g->near,g->near_count*4);return 2+g->eligible_count+g->near_count;}
int region_roll(wc3AiRegions_t const *g,wc3Random_t *r,float const *o,float s,float *p){return wc3_ai_regions_roll(g,r,o,s,p);}
unsigned region_first_roll(wc3Random_t *r){return 1+wc3_random_range(r,3);}
