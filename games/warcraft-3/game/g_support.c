#include "g_local.h"
#include "games/warcraft-3/common/wc3_math.h"

/* Terrain/deck consumers share one authoritative model query. A prepared mesh
 * miss must stay a miss; only the existing unprepared/animated path uses the
 * footprint rectangle until those model poses are implemented. */
bool S_GetWalkableSupport(vec2_t point,float overlap,float *height) {
    bool hit=false;float highest=-FLT_MAX;
    for (edict_t *surface=level.ground_surfaces;surface;surface=surface->ground_next) {
        if(!surface->inuse||surface->destructable->dead||!surface->destructable->placement_solid)continue;
        float deck;
        int mesh=G_WalkableModelHeight(surface,point,&deck);
        if(!mesh)continue;
        if(mesh<0) {
            if(!surface->pathtex)continue;
            pathTexTransform_t transform=CM_GetPathTexTransform(surface);
            float cell=CM_PathCellWorldSize();
            bool elevator=surface->class_id==MAKEFOURCC('D','T','r','x')||surface->class_id==MAKEFOURCC('D','T','r','f');
            float extra=elevator?overlap:0;
            if(fabsf(point.x-surface->s.origin.x)>transform.width*cell*.5f+extra||
                fabsf(point.y-surface->s.origin.y)>transform.height*cell*.5f+extra)continue;
            deck=surface->s.origin.z+(elevator?surface->destructable->occluder_height:0);
        }
        if(!hit||deck>highest)highest=deck;
        hit=true;
    }
    if(hit)*height=highest;
    return hit;
}

float S_GetLocationSupport(vec2_t point) {
    float height=CM_GetHeightAtPoint(point.x,point.y),deck;
    if(S_GetWalkableSupport(point,0,&deck)&&deck>height)height=deck;
    float water=CM_GetWaterHeightAtPoint(point.x,point.y);
    return water>height?water:height;
}

/* Terrain+7a4: map-start flyer support is independent of live ground support.
 * Keep one immutable completed field, shared by every flyer. */
typedef struct {
    float *values;
    uint32_t width, height;
    float cell;
    vec2_t origin;
    bool finalized;
} flightSupport_t;

/* Native743810/73fc80 use SSE float arithmetic, not Warcraft software
 * scalars. Explicit stores preserve each rounding and prevent contraction. */
static float support_add(float a,float b) { volatile float r=a+b; return r; }
static float support_sub(float a,float b) { volatile float r=a-b; return r; }
static float support_mul(float a,float b) { volatile float r=a*b; return r; }
static float support_div(float a,float b) { volatile float r=a/b; return r; }

static flightSupport_t flight_support, slope_support, water_support;
static flightSupport_t presentation_mips[3];

/*748470 builds mode0 from four packed terrain vertices per128-unit tile.
 * It is distinct from both physical height and the maximized flyer field. */
static void slope_support_initialize(void) {
    gi.MemFree(slope_support.values); slope_support=(flightSupport_t){0};
    if (!world.map || !world.map->vertices || world.map->width<3 || world.map->height<3) return;
    flightSupport_t *f=&slope_support;
    f->width=world.map->width-1; f->height=world.map->height-1;
    f->cell=TILE_SIZE; f->origin=world.map->center;
    f->values=gi.MemAlloc((size_t)f->width*f->height*sizeof(float));
    war3mapVertex_t const *vertices=world.map->vertices;
    FOR_LOOP(y,f->height) FOR_LOOP(x,f->width) {
        float z=0;
        /* Native corner order:00,10,01,11 (6fb85aa4/6fb85ab4). */
        static uint8_t const offsets[4][2]={{0,0},{1,0},{0,1},{1,1}};
        FOR_LOOP(k,4) {
            war3mapVertex_t const *v=vertices+(size_t)(y+offsets[k][1])*world.map->width+x+offsets[k][0];
            float h=support_sub(support_div((float)v->accurate_height-8192.0f,4),HEIGHT_COR);
            h=support_add(h,(float)v->level*TILE_SIZE);
            z=support_add(h,z);
        }
        f->values[(size_t)y*f->width+x]=support_mul(z,.25f);
    }
    /*748470: four cell-centred grids; each higher layer averages the previous
     * four cells in00,10,01,11 order. Prepare once, never during placement. */
    FOR_LOOP(mode,3) {
        flightSupport_t const *previous=mode?presentation_mips+mode-1:&slope_support;
        flightSupport_t *next=presentation_mips+mode;
        if(previous->width<4 || previous->height<4)break;
        next->width=previous->width/2;next->height=previous->height/2;
        next->cell=previous->cell*2;next->origin=previous->origin;
        next->values=gi.MemAlloc((size_t)next->width*next->height*sizeof(float));
        FOR_LOOP(y,next->height)FOR_LOOP(x,next->width) {
            float const *a=previous->values+(size_t)y*2*previous->width+x*2;
            float z=support_add(a[0],0);z=support_add(z,a[1]);
            z=support_add(z,a[previous->width]);z=support_add(z,a[previous->width+1]);
            next->values[(size_t)y*next->width+x]=support_mul(z,.25f);
        }
    }
}

/*7446e0's corner adjustment uses the packed ramp topology, independently
 * of physical interpolation and the SSE presentation grid.7452b0 enumerates
 * each3x3 neighborhood in x-major order. */
static war3mapVertex_t const *support_vertex(int x,int y) {
    return (uint32_t)x<world.map->width && (uint32_t)y<world.map->height ?
        (war3mapVertex_t const *)world.map->vertices+(size_t)y*world.map->width+x : NULL;
}

/*74a910: highest ramp layer among the four touching terrain cells. The
 * minimum starts at Terrain988, derived from authored Terrain.MaxHeight. */
static bool support_ramp_high(int x,int y,int cap) {
    int low=cap,high=0;
    FOR_LOOP(dy,2)FOR_LOOP(dx,2) {
        int cx=x-dx,cy=y-dy;
        if((uint32_t)cx>=world.map->width-1 || (uint32_t)cy>=world.map->height-1)continue;
        FOR_LOOP(vy,2)FOR_LOOP(vx,2) {
            war3mapVertex_t const *v=support_vertex(cx+vx,cy+vy);
            if(v->ramp){low=MIN(low,v->level&15);high=MAX(high,v->level&15);}
        }
    }
    return low!=high && (support_vertex(x,y)->level&15)==high;
}

/*744750 returns-1/0/1/2; security-cookie epilogues obscure these integer
 * results in the decompilation. Keep the assembly's all-ramp distinction. */
static int support_ramp_kind(int x,int y,int cap) {
    war3mapVertex_t const *self=support_vertex(x,y),*v[9];
    if(!self->ramp)return -1;
    if(support_ramp_high(x,y,cap))return 0;
    int low=self->level&15,high=low,ramp_low=low,ramp_high=low;
    bool all=true;
    FOR_LOOP(dx,3)FOR_LOOP(dy,3) {
        war3mapVertex_t const *n=v[dx*3+dy]=support_vertex(x+(int)dx-1,y+(int)dy-1);
        if(!n){all=false;continue;}
        int layer=n->level&15;low=MIN(low,layer);high=MAX(high,layer);
        if(!n->ramp){all=false;continue;}
        ramp_low=MIN(ramp_low,layer);ramp_high=MAX(ramp_high,layer);
    }
    if((all && high==low) || ramp_high-ramp_low>1)return -1;
    bool a=v[1] && v[7] && v[1]->ramp && v[7]->ramp && ((v[1]->level^v[7]->level)&15);
    bool b=v[3] && v[5] && v[3]->ramp && v[5]->ramp && ((v[3]->level^v[5]->level)&15);
    if(a && b)return high-low<=1?2:-1;
    if(a || b)return high-low<=1?1:-1;
    return all?2:0;
}

static char support_ramp_type(int x,int y,int cap) {
    bool high=support_ramp_high(x,y,cap);
    if(!support_vertex(x,y)->ramp)return high?'B':'A';
    if(high)return 'H';
    int kind=support_ramp_kind(x,y,cap);
    return kind>=0?"LRS"[kind]:'!';
}

/*73a380 admits only the authored corner/edge patterns. No neighborhood
 * outside the map can supply the quarter/three-quarter adjustment. */
static int support_ramp_corner(int x,int y,int cap) {
    char type[9];int lows=0,highs=0,last_low=-1,last_high=-1;
    FOR_LOOP(dx,3)FOR_LOOP(dy,3) {
        int nx=x+(int)dx-1,ny=y+(int)dy-1;
        if(!support_vertex(nx,ny))return -1;
        type[dx*3+dy]=support_ramp_type(nx,ny,cap);
    }
    static uint8_t const corners[4][2]={{0,0},{0,2},{2,0},{2,2}};
    FOR_LOOP(i,4) {
        char c=type[corners[i][0]*3+corners[i][1]];
        if(c=='L'){lows++;last_low=i;}
        if(c=='H'){highs++;last_high=i;}
    }
    if(lows==1 && highs==3) {
        int a=corners[last_low][0],b=corners[last_low][1];
        return type[1+a*3]=='R' && type[3+b]=='R' && type[7-a*3]=='H' && type[5-b]=='H'?1:-1;
    }
    if(lows==2 && highs==2) {
        int a=corners[last_low][0],b=corners[last_low][1];
        return type[8-a*3-b]=='L' && type[1]=='R' && type[7]=='R' && type[3]=='R' && type[5]=='R'?2:-1;
    }
    if(lows==3 && highs==1) {
        int a=corners[last_high][0],b=corners[last_high][1];
        return type[1+a*3]=='R' && type[3+b]=='R' && type[7-a*3]=='L' && type[5-b]=='L'?0:-1;
    }
    return -1;
}

static float support_ramp_offset(int x,int y,int cap) {
    if(!support_vertex(x,y)->ramp)return 0;
    char type=support_ramp_type(x,y,cap);
    if(type=='R')return wc3_mul(.5f,128);
    if(type!='S')return 0;
    int corner=support_ramp_corner(x,y,cap);
    /*74470c/74470f/744712 map0->.25,1->.75,2->.5. */
    static float const fractions[]={.25f,.75f,.5f};
    return corner>=0?wc3_mul(fractions[corner],128):0;
}

/* Storm574 (1503b140) parses metadata through binary64 SSE and rounds once
 * to float. JASS S2R070de0 is a different parser (no exponents, truncation).
 * Explicit stores retain each native binary64 rounding and prohibit FMA. */
static double support_double_add(double a,double b) { volatile double v=a+b;return v; }
static double support_double_mul(double a,double b) { volatile double v=a*b;return v; }
static double support_double_div(double a,double b) { volatile double v=a/b;return v; }
static double support_decimal_power(int32_t exponent) {
    uint32_t n=exponent<0?0u-(uint32_t)exponent:(uint32_t)exponent;
    double base=10,result=1;
    for(;;) {
        if(n&1)result=support_double_mul(result,base);
        n>>=1;if(!n)break;
        base=support_double_mul(base,base);
    }
    return exponent<0?support_double_div(1,result):result;
}

static float support_metadata_float(cstring_t text) {
    if(!text)return 0;
    bool negative=*text=='-';if(negative)text++;
    cstring_t start=text;uint32_t chunk=0;double value=0;
    while((unsigned char)*text>='0' && (unsigned char)*text<='9') {
        chunk=chunk*10+(*text++-'0');
        if(chunk>=0x19999999u) {
            value=support_double_add(support_double_mul(value,support_decimal_power((int32_t)(text-start))),chunk);
            chunk=0;start=text;
        }
    }
    value=value!=0?support_double_add(support_double_mul(value,support_decimal_power((int32_t)(text-start))),chunk):chunk;
    if(*text=='.') {
        text++;int32_t place=-1;
        while((unsigned char)*text>='0' && (unsigned char)*text<='9') {
            /*1503a390 initializes its first20-place table with this same
             * repeated-squaring power and multiply. Later digits do it inline. */
            value=support_double_add(value,support_double_mul(*text++-'0',support_decimal_power(place--)));
        }
    }
    if(*text=='e' || *text=='E') {
        text++;if(*text=='+')text++;
        bool minus=*text=='-';if(minus)text++;
        uint32_t n=0;
        while((unsigned char)*text>='0' && (unsigned char)*text<='9')n=n*10+(*text++-'0');
        if(minus)n=0u-n;
        int32_t exponent;memcpy(&exponent,&n,sizeof(exponent));
        value=support_double_mul(value,support_decimal_power(exponent));
    }
    return (float)(negative?-value:value);
}

/*729f58/729f68: CVTTSS2SI returns integer-indefinite on unordered or out-of-
 * range input. Do not use Warcraft070120's wrapping R2I conversion here. */
static int32_t support_height_cap(float maximum) {
    float value=support_div(support_sub(maximum,-256),128);
    return value>=-2147483648.0f && value<2147483648.0f?(int32_t)value:INT32_MIN;
}

/*7488a0 creates one water type iff any packed vertex carries water. It
 * selects <terrain tileset>Sha, falling back to LSha when that row is absent.
 *748ab0 uses the database height directly, independently of surface rendering.
 * This immutable derived input is rebuilt from the loaded map after load;
 * unlike the finalized flyer field it contains no removed-object history. */
static void water_support_initialize(void) {
    if (water_support.values || !world.map || !world.map->vertices ||
        world.map->width<2 || world.map->height<2) return;
    flightSupport_t *f=&water_support;
    f->width=world.map->width-1;f->height=world.map->height-1;
    f->cell=TILE_SIZE;f->origin=world.map->center;
    f->values=gi.MemAlloc((size_t)f->width*f->height*sizeof(float));
    war3mapVertex_t const *vertices=world.map->vertices;
    bool water=false;
    FOR_LOOP(i,(size_t)world.map->width*world.map->height)if(vertices[i].water){water=true;break;}
    /*729f08..729f6c: SSE(MaxHeight-(-256))/128, CVTTSS2SI. */
    cstring_t maximum=Stb_IniCacheFind(&game.config.misc,"Terrain","MaxHeight");
    int32_t cap=support_height_cap(support_metadata_float(maximum));
    float offset=0;
    if(water) {
        WaterData_t const *row=G_WaterData(MAKEFOURCC(world.map->tileset,'S','h','a'));
        if(!row->id) {
            fprintf(stderr,"WC3 prepared water: missing Water.slk row %cSha; native default is LSha\n",world.map->tileset?world.map->tileset:'?');
            row=G_WaterData(MAKEFOURCC('L','S','h','a'));
            if(!row->id)fprintf(stderr,"WC3 prepared water: missing Water.slk LSha height; metadata scalar is unresolved\n");
        }
        offset=wc3_decimal(row->height);
    }
    FOR_LOOP(y,f->height)FOR_LOOP(x,f->width) {
        float sum=0;
        static uint8_t const corners[4][2]={{0,0},{1,0},{0,1},{1,1}};
        FOR_LOOP(k,4) {
            war3mapVertex_t const *v=vertices+(size_t)(y+corners[k][1])*world.map->width+x+corners[k][0];
            float ground=wc3_sub(wc3_float(((v->accurate_height&0x3fffu)|0x22c000u)<<9),6144);
            ground=wc3_add(ground,wc3_add(-256,wc3_mul(wc3_float(wc3_from_int(v->level&15)),128)));
            ground=wc3_add(ground,support_ramp_offset(x+corners[k][0],y+corners[k][1],cap));
            float height=ground;
            if(water) {
                float z=wc3_add(wc3_sub(wc3_float(((uint32_t)(v->waterlevel&0x3fff)<<9)|0x45800000u),6144),offset);
                if(ground<=z)height=z;
            }
            sum=wc3_add(sum,height);
        }
        f->values[(size_t)y*f->width+x]=wc3_mul(.25f,sum);
    }
}

/*7455a0: signed fine integers /4 truncate toward zero, then clamp. No
 * interpolation or support from live widgets is part of this query. */
float S_GetPreparedWaterSupport(vec2_t fine) {
    water_support_initialize();
    flightSupport_t const *f=&water_support;
    if(!f->values)return 0;
    uint32_t ux=wc3_int_bits(wc3_float_bits(fine.x)),uy=wc3_int_bits(wc3_float_bits(fine.y));
    int32_t x,y;memcpy(&x,&ux,sizeof(x));memcpy(&y,&uy,sizeof(y));
    x=MAX(0,MIN(x/4,(int32_t)f->width-1));
    y=MAX(0,MIN(y/4,(int32_t)f->height-1));
    return f->values[(size_t)y*f->width+x];
}

void S_ClearFlightSupport(void) {
    gi.MemFree(water_support.values);water_support=(flightSupport_t){0};
    FOR_LOOP(i,3){gi.MemFree(presentation_mips[i].values);presentation_mips[i]=(flightSupport_t){0};}
    gi.MemFree(flight_support.values);
    flight_support = (flightSupport_t){0};
    gi.MemFree(slope_support.values); slope_support=(flightSupport_t){0};
}

void S_InitFlightSupport(void) {
    S_ClearFlightSupport();
    slope_support_initialize();
    water_support_initialize();
    if (!world.map || !world.map->vertices || world.map->width < 2 || world.map->height < 2) return;
    flightSupport_t *f = &flight_support;
    f->width = (world.map->width-1)*4;
    f->height = (world.map->height-1)*4;
    f->cell = TILE_SIZE/4;
    f->origin = world.map->center;
    f->values = gi.MemAlloc((size_t)f->width*f->height*sizeof(float));
    war3mapVertex_t const *vertices = world.map->vertices;
    int previous_y = -1;
    FOR_LOOP(y,f->height) {
        uint32_t vy = (y+2)/4;
        float *row = f->values+(size_t)y*f->width;
        if ((int)vy == previous_y) {
            memcpy(row,row-f->width,f->width*sizeof(float));
            continue;
        }
        previous_y = vy;
        int previous_x = -1;
        float z = 0;
        FOR_LOOP(x,f->width) {
            uint32_t vx = (x+2)/4;
            if ((int)vx != previous_x) {
                previous_x = vx;
                float sx = f->origin.x+vx*TILE_SIZE, sy = f->origin.y+vy*TILE_SIZE;
                z = CM_GetHeightAtPoint(sx,sy);
                if (vertices[(size_t)vy*world.map->width+vx].water)
                    z = MAX(z,CM_GetWaterHeightAtPoint(sx,sy));
            }
            row[x] = z;
        }
    }
}

/* Retail74d4b0 compares the left endpoint, strict-rise/plateau/fall peaks,
 * then the right endpoint. Preserve that candidate order even for signed
 * zero. A deque over those peaks makes each pass O(cells), for any radius. */
static void flight_support_maximum(float const *in, float *out, uint32_t width,
                                   uint32_t height, uint32_t radius, uint32_t *queue) {
    FOR_LOOP(y,height) {
        uint32_t head = 0, tail = 0, next = 1;
        bool rising = false;
        float const *row = in+(size_t)y*width;
        FOR_LOOP(x,width) {
            uint32_t last = (uint32_t)MIN((uint64_t)x+radius,width-1);
            uint32_t first = x > radius ? x-radius : 0;
            while (next <= last) {
                if (rising) {
                    if (row[next-1] > row[next]) {
                        uint32_t peak = next-1;
                        while (tail > head && row[queue[tail-1]] < row[peak]) tail--;
                        queue[tail++] = peak;
                        rising = false;
                    }
                } else if (row[next] > row[next-1]) rising = true;
                next++;
            }
            while (head < tail && queue[head] <= first) head++;
            float value = row[first];
            if (head < tail && row[queue[head]] > value) value = row[queue[head]];
            if (row[last] > value) value = row[last];
            out[(size_t)x*height+y] = value;
        }
    }
}

static uint32_t flight_support_setting(cstring_t name) {
    cstring_t value = Stb_IniCacheFind(&game.config.misc,"FlyerMap",name);
    int32_t n = value ? (int32_t)strtol(value,NULL,10) : 0;
    return n > 0 ? (uint32_t)n : 0;
}

void S_FinalizeFlightSupport(void) {
    flightSupport_t *f = &flight_support;
    if (f->finalized) return;
    if (!f->values) S_InitFlightSupport();
    if (!f->values) return;
    /* 24ffc0 enumerates initial widgets before the maximum/smoothing passes.
     * Its rectangle is inclusive; a texture's upper edge contributes too. */
    FOR_LOOP(i,globals.num_edicts) {
        edict_t const *ent = g_edicts+i;
        if (!ent->inuse || !G_IsDestructable(ent) || !ent->data.DestructableData || !ent->pathtex ||
            !ent->destructable->placement_solid) continue;
        float lift = ent->data.DestructableData->flyHeight;
        pathTexTransform_t t = CM_GetPathTexTransform(ent);
        int cx = (int)((ent->s.origin.x-f->origin.x)/f->cell);
        int cy = (int)((ent->s.origin.y-f->origin.y)/f->cell);
        int x0 = cx-(int)t.width/2, y0 = cy-(int)t.height/2;
        int x1 = MIN((int)f->width-1,x0+(int)t.width);
        int y1 = MIN((int)f->height-1,y0+(int)t.height);
        for (int y = MAX(0,y0); y <= y1; y++) for (int x = MAX(0,x0); x <= x1; x++) {
            float z = CM_GetHeightAtPoint(f->origin.x+((x+2)/4)*TILE_SIZE,
                                         f->origin.y+((y+2)/4)*TILE_SIZE);
            float *value = f->values+(size_t)y*f->width+x;
            z = support_add(z,lift);
            if (*value < z) *value = z;
        }
    }
    uint32_t radius = flight_support_setting("MaximizeRadius");
    float *scratch = gi.MemAlloc((size_t)f->width*f->height*sizeof(float));
    uint32_t *queue = gi.MemAlloc(MAX(f->width,f->height)*sizeof(uint32_t));
    flight_support_maximum(f->values,scratch,f->width,f->height,radius,queue);
    flight_support_maximum(scratch,f->values,f->height,f->width,radius,queue);
    gi.MemFree(queue);
    uint32_t levels = flight_support_setting("SmoothLevels");
    while (levels-- && f->width >= 4 && f->height >= 4) {
        uint32_t width = f->width/2, height = f->height/2;
        FOR_LOOP(y,height) FOR_LOOP(x,width) {
            float const *a = f->values+(size_t)(y*2)*f->width+x*2;
            float z = support_add(a[0],0);
            z = support_add(z,a[1]); z = support_add(z,a[f->width]); z = support_add(z,a[f->width+1]);
            scratch[(size_t)y*width+x] = support_mul(z,0.25f);
        }
        memcpy(f->values,scratch,(size_t)width*height*sizeof(float));
        f->width = width; f->height = height; f->cell *= 2;
    }
    gi.MemFree(scratch);
    float *completed = gi.MemAlloc((size_t)f->width*f->height*sizeof(float));
    memcpy(completed,f->values,(size_t)f->width*f->height*sizeof(float));
    gi.MemFree(f->values); f->values = completed; f->finalized = true;
}

static bool support_interpolate(flightSupport_t const *f,vec2_t point,float *height) {
    if (!f->values) return false;
    float x = support_sub(support_div(support_sub(point.x,f->origin.x),f->cell),0.5f);
    float y = support_sub(support_div(support_sub(point.y,f->origin.y),f->cell),0.5f);
    int ix = (int)x, iy = (int)y;
    x = support_sub(x,(float)ix); y = support_sub(y,(float)iy);
    if (ix < 0) ix = 0,x = 0;
    else if (ix > (int)f->width-2) ix = f->width-2,x = 0;
    if (iy < 0) iy = 0,y = 0;
    else if (iy > (int)f->height-2) iy = f->height-2,y = 0;
    float const *a = f->values+(size_t)iy*f->width+ix;
    float nx = support_sub(1,x), ny = support_sub(1,y);
    float z = support_mul(support_mul(a[1],x),ny);
    z = support_add(z,support_mul(support_mul(nx,a[0]),ny));
    z = support_add(z,support_mul(support_mul(nx,a[f->width]),y));
    *height = support_add(z,support_mul(support_mul(a[f->width+1],x),y));
    return true;
}

bool S_GetPresentationSupport(vec2_t point,uint32_t layer,float *height) {
    if(layer>3)return false;
    if(!slope_support.values)slope_support_initialize();
    return support_interpolate(layer?presentation_mips+layer-1:&slope_support,point,height);
}

bool S_GetFlightSupport(vec2_t point,float *height) {
    return support_interpolate(&flight_support,point,height);
}

static vec3_t slope_cross(vec3_t a,vec3_t b) {
    return (vec3_t){support_sub(support_mul(a.y,b.z),support_mul(a.z,b.y)),
        support_sub(support_mul(a.z,b.x),support_mul(a.x,b.z)),
        support_sub(support_mul(a.x,b.y),support_mul(a.y,b.x))};
}
static vec3_t slope_normalize(vec3_t v) {
    float square=support_add(support_mul(v.y,v.y),support_mul(v.x,v.x));
    square=support_add(square,support_mul(v.z,v.z));
    float inverse=support_div(1,(float)sqrt((double)square));
    return (vec3_t){support_mul(v.x,inverse),support_mul(v.y,inverse),support_mul(v.z,inverse)};
}

/*66d990: the ordinary ground-unit three-tap presentation path. Never change
 * the actor heading or physical support. Flying bank and zero-radius triangle
 * normals have separate owners; those paths keep the existing upright pose. */
void S_RefreshUnitSlope(edict_t *self) {
    self->s.pose_offset=(orientation_t){0};
    UnitUI_t const *ui=self->data.UnitUI;
    if (!ui || (self->aiflags & AI_FLYING)) return;
    float pitch_limit=wc3_degrees_to_radians(ui->maxPitchDegrees);
    float roll_limit=wc3_degrees_to_radians(ui->maxRollDegrees);
    if (fabsf(pitch_limit)<.001f && fabsf(roll_limit)<.001f) return;
    float radius=ui->elevationSampleRadius;
    if (fabsf(radius)<2.384185791015625e-7f) return;
    if (!slope_support.values) slope_support_initialize();
    if (!slope_support.values) return;
    float heading=self->s.angle;
    vec3_t forward={(float)cos((double)heading),(float)sin((double)heading),0};
    float rear=-radius,low=support_sub(heading,.78539818525314331055f),high=support_add(heading,.78539818525314331055f);
    vec3_t points[3]={
        {support_add(self->s.origin.x,support_mul(forward.x,rear)),support_add(self->s.origin.y,support_mul(forward.y,rear)),0},
        {support_add(self->s.origin.x,support_mul((float)cos((double)low),radius)),support_add(self->s.origin.y,support_mul((float)sin((double)low),radius)),0},
        {support_add(self->s.origin.x,support_mul((float)cos((double)high),radius)),support_add(self->s.origin.y,support_mul((float)sin((double)high),radius)),0}};
    float maximum=-FLT_MAX;
    FOR_LOOP(i,3) {
        float z,deck;
        support_interpolate(&slope_support,(vec2_t){points[i].x,points[i].y},&z);
        if(S_GetWalkableSupport((vec2_t){points[i].x,points[i].y},0,&deck) && deck>z)z=deck;
        points[i].z=z;
        if(maximum<=z)maximum=z;
    }
    float discontinuity=support_mul(radius,2.5f);
    FOR_LOOP(i,3)if(support_sub(maximum,points[i].z)>=discontinuity)points[i].z=maximum;
    vec3_t a={support_sub(points[1].x,points[0].x),support_sub(points[1].y,points[0].y),support_sub(points[1].z,points[0].z)};
    vec3_t b={support_sub(points[2].x,points[0].x),support_sub(points[2].y,points[0].y),support_sub(points[2].z,points[0].z)};
    vec3_t normal=slope_normalize(slope_cross(a,b));
    vec3_t right=slope_cross(normal,forward);
    forward=slope_normalize(slope_cross(right,normal)); right=slope_normalize(right);
    float yaw,pitch,roll;
    if(forward.z>=1 || forward.z<=-1) {
        /*1c7ba0 singular branch; authored positive radii ordinarily stay regular. */
        yaw=(float)atan2((double)-right.x,(double)right.y);
        pitch=forward.z>=1?-(float)M_PI_2:(float)M_PI_2;roll=0;
    } else {
        yaw=(float)atan2((double)forward.y,(double)forward.x);
        pitch=(float)asin((double)-forward.z);
        roll=(float)atan2((double)right.z,(double)normal.z);
    }
    if(pitch<-pitch_limit)pitch=-pitch_limit;else if(pitch>pitch_limit)pitch=pitch_limit;
    if(roll<-roll_limit)roll=-roll_limit;else if(roll>roll_limit)roll=roll_limit;
    float offset=support_sub(yaw,heading);
    if(offset<-(float)M_PI)offset=support_add(offset,(float)(2*M_PI));
    else if(offset>(float)M_PI)offset=support_sub(offset,(float)(2*M_PI));
    self->s.pose_offset=(orientation_t){offset,-pitch,roll};
}

/* The completed values describe map-start history, including objects later
 * removed. Rebuilding from the currently live edicts would change a replay. */
bool S_WriteFlightSupport(FILE *file) {
    flightSupport_t const *f = &flight_support;
    uint32_t header[] = { f->width,f->height,wc3_float_bits(f->cell),
        wc3_float_bits(f->origin.x),wc3_float_bits(f->origin.y),f->finalized };
    size_t count = (size_t)f->width*f->height;
    return fwrite(header,sizeof(header),1,file) == 1 &&
        (!count || fwrite(f->values,sizeof(float),count,file) == count);
}

bool S_ReadFlightSupport(FILE *file) {
    uint32_t h[6];
    if (fread(h,sizeof(h),1,file) != 1) return false;
    flightSupport_t next = { .width = h[0], .height = h[1],
        .cell = wc3_float(h[2]), .origin = {wc3_float(h[3]),wc3_float(h[4])},
        .finalized = h[5] != 0 };
    if (h[5] > 1 || (!!h[0] != !!h[1])) return false;
    if (!h[0]) {
        if (h[2] || h[3] || h[4] || h[5]) return false;
        S_ClearFlightSupport(); return true;
    }
    if (!world.map || world.map->width < 2 || world.map->height < 2 ||
        !isfinite(next.cell) || next.cell < TILE_SIZE/4 ||
        !isfinite(next.origin.x) || !isfinite(next.origin.y) ||
        next.origin.x != world.map->center.x || next.origin.y != world.map->center.y) return false;
    uint32_t width = (world.map->width-1)*4, height = (world.map->height-1)*4;
    float cell = TILE_SIZE/4;
    while (cell < next.cell && width >= 4 && height >= 4) width /= 2,height /= 2,cell *= 2;
    if (next.cell != cell || next.width != width || next.height != height ||
        (!next.finalized && cell != TILE_SIZE/4)) return false;
    size_t count = (size_t)next.width*next.height;
    long start = ftell(file);
    if (start < 0 || fseek(file,0,SEEK_END)) return false;
    long end = ftell(file);
    if (fseek(file,start,SEEK_SET) || end < start || count > (size_t)(end-start)/sizeof(float)) return false;
    next.values = gi.MemAlloc(count*sizeof(float));
    if (fread(next.values,sizeof(float),count,file) != count) {
        gi.MemFree(next.values); return false;
    }
    FOR_LOOP(i,count) if (!isfinite(next.values[i])) {
        gi.MemFree(next.values); return false;
    }
    S_ClearFlightSupport(); flight_support = next; return true;
}

#ifdef BZ_TESTS
#include "shared/test.h"
#include "tests/fixtures/retail_flight_support228.h"

TEST(wc3_support, maximum_filter_matches_original_instructions) {
    FOR_LOOP(c,sizeof(flight228_max)/sizeof(*flight228_max)) {
        size_t count = (size_t)flight228_max[c].width*flight228_max[c].height;
        float *input = gi.MemAlloc(count*sizeof(float));
        float *output = gi.MemAlloc(count*sizeof(float));
        uint32_t *queue = gi.MemAlloc(flight228_max[c].width*sizeof(uint32_t));
        FOR_LOOP(i,count) input[i] = wc3_float(flight228_max[c].input[i]);
        flight_support_maximum(input,output,flight228_max[c].width,
            flight228_max[c].height,flight228_max[c].radius,queue);
        FOR_LOOP(i,count) T_EQ(wc3_float_bits(output[i]),flight228_max[c].output[i]);
        gi.MemFree(queue); gi.MemFree(output); gi.MemFree(input);
    }
}

TEST(wc3_support, interpolation_matches_original_instructions_at_edges_and_negative_coordinates) {
    flightSupport_t saved = flight_support;
    float grid[40];
    FOR_LOOP(i,40) grid[i] = wc3_float(flight228_grid[i]);
    flight_support = (flightSupport_t){ .values=grid,.width=8,.height=5,
        .cell=96,.origin={-352,224},.finalized=true };
    FOR_LOOP(i,sizeof(flight228_samples)/sizeof(*flight228_samples)) {
        float z = 0;
        T_ASSERT(S_GetFlightSupport((vec2_t){wc3_float(flight228_samples[i].x),
            wc3_float(flight228_samples[i].y)},&z));
        T_EQ(wc3_float_bits(z),flight228_samples[i].z);
    }
    flight_support = saved;
}

TEST(wc3_support, field_codec_preserves_native_grid_and_rejects_invalid_payloads_atomically) {
    extern void reset_entities(void), setup_test_world(void);
    reset_entities(); setup_test_world();
    world.map->width = world.map->height = 17;
    world.map->center = (vec2_t){0,0};
    flight_support = (flightSupport_t){ .values=gi.MemAlloc(64*sizeof(float)),
        .width=8,.height=8,.cell=256,.finalized=true };
    FOR_LOOP(i,64) flight_support.values[i] = wc3_float(flight228_map[i]);
    FILE *file = tmpfile(); T_NOT_NULL(file);
    T_ASSERT(S_WriteFlightSupport(file)); rewind(file);
    S_ClearFlightSupport(); T_ASSERT(S_ReadFlightSupport(file)); fclose(file);
    FOR_LOOP(i,64) T_EQ(wc3_float_bits(flight_support.values[i]),flight228_map[i]);
    FOR_LOOP(c,8) {
        uint32_t h[] = {8,8,wc3_float_bits(256),0,0,1};
        uint32_t values[64]; memcpy(values,flight228_map,sizeof(values));
        switch (c) {
        case 0: h[0] = UINT32_MAX; break;
        case 1: h[2] = wc3_float_bits(192); break;
        case 2: h[3] = wc3_float_bits(32); break;
        case 3: h[5] = 2; break;
        case 4: h[5] = 0; break;
        case 5: values[63] = 0x7fc00000; break;
        case 6: h[0] = h[1] = 0; break;
        case 7: break; /* Truncated values. */
        }
        float *saved = flight_support.values;
        file = tmpfile(); T_NOT_NULL(file);
        T_EQ(fwrite(h,sizeof(h),1,file),1);
        T_EQ(fwrite(values,sizeof(uint32_t),c==7 ? 63 : 64,file),c==7 ? 63 : 64);
        rewind(file); T_ASSERT(!S_ReadFlightSupport(file)); fclose(file);
        T_ASSERT(flight_support.values == saved);
        FOR_LOOP(i,64) T_EQ(wc3_float_bits(saved[i]),flight228_map[i]);
    }
    S_ClearFlightSupport(); reset_entities(); setup_test_world();
}
#endif

#ifdef BZ_TESTS
#include "tests/fixtures/retail_slope272.h"
TEST(wc3_support, presentation_height_grid_matches_read_only_retail_capture) {
    extern void reset_entities(void),setup_test_world(void);
    reset_entities();setup_test_world();
    world.map->width=world.map->height=17;world.map->center=(vec2_t){0,0};
    war3mapVertex_t *v=world.map->vertices;
    FOR_LOOP(y,17)FOR_LOOP(x,17) {
        bool water=y<=10 && x>=4 && x<=12;
        v[y*17+x]=(war3mapVertex_t){.accurate_height=water?
            0x2000-4*((x==4||x==5||x==11||x==12)?64:192):0x2000,
            .waterlevel=8550,.water=water,.level=y>=12&&x>=8?3:2};
    }
    S_InitFlightSupport();T_EQ(slope_support.width,16);T_EQ(slope_support.height,16);
    FOR_LOOP(i,256)T_EQ(wc3_float_bits(slope_support.values[i]),slope272_grid[i]);
    reset_entities();setup_test_world();
}
#endif

#ifdef BZ_TESTS
#include "tests/fixtures/retail_bridge311_grids.h"
TEST(wc3_support, bridge311_all_sampling_layers_match_retained_retail_grids) {
    extern void reset_entities(void),setup_test_world(void);
    reset_entities();setup_test_world();
    world.map->width=world.map->height=17;world.map->center=(vec2_t){0,0};
    war3mapVertex_t *vertices=world.map->vertices;
    FOR_LOOP(y,17)FOR_LOOP(x,17) {
        bool water=y<=10 && x>=4 && x<=12;
        vertices[y*17+x]=(war3mapVertex_t){.accurate_height=water?
            0x2000-4*((x==4||x==5||x==11||x==12)?64:192):0x2000,
            .waterlevel=8550,.water=water,.level=y>=12&&x>=8?3:2};
    }
    FOR_LOOP(cold,2) {
        if(!cold)S_InitFlightSupport();
        else {S_ClearFlightSupport();float z;T_ASSERT(S_GetPresentationSupport((vec2_t){1024,672},1,&z));}
        FOR_LOOP(layer,4) {
            flightSupport_t const *f=layer?presentation_mips+layer-1:&slope_support;
            T_EQ(f->width,bridge311_grids[layer].shape[0]);T_EQ(f->height,bridge311_grids[layer].shape[1]);
            T_EQ(wc3_float_bits(f->cell),bridge311_grids[layer].shape[2]);
            FOR_LOOP(i,f->width*f->height)T_EQ(wc3_float_bits(f->values[i]),bridge311_grids[layer].values[i]);
        }
    }
    float value=123;T_ASSERT(!S_GetPresentationSupport((vec2_t){1024,672},4,&value));T_EQ(value,123);
    S_ClearFlightSupport();reset_entities();setup_test_world();
}
#endif


#ifdef BZ_TESTS
#include "tests/fixtures/retail_widgetgoal_water.h"
TEST(wc3_support, widgetgoal_prepared_water_grid_matches_repeated_retail_input) {
    extern void reset_entities(void),setup_test_world(void);
    reset_entities();setup_test_world();
    world.map->width=world.map->height=17;world.map->center=(vec2_t){0,0};world.map->tileset='L';
    war3mapVertex_t *v=world.map->vertices;
    FOR_LOOP(y,17)FOR_LOOP(x,17) {
        bool water=y<=10 && x>=4 && x<=12;
        v[y*17+x]=(war3mapVertex_t){.accurate_height=water?
            0x2000-4*((x==4||x==5||x==11||x==12)?64:192):0x2000,
            .waterlevel=8550,.water=water,.level=y>=12&&x>=8?3:2};
    }
    S_InitFlightSupport();
    FOR_LOOP(y,16)FOR_LOOP(x,16)
        T_EQ(wc3_float_bits(S_GetPreparedWaterSupport((vec2_t){x*4+1,y*4+3})),widgetgoal_water_grid[y*16+x]);
    static vec2_t const edge[]={{-7,-1},{-.9f,0},{63.999f,63.999f},{64,64},{400,400}};
    static unsigned const indices[]={0,0,255,255,255};
    FOR_LOOP(i,5)T_EQ(wc3_float_bits(S_GetPreparedWaterSupport(edge[i])),widgetgoal_water_grid[indices[i]]);
    /* No water vertices means no water type, even with authored raw levels. */
    FOR_LOOP(i,17*17)v[i]=(war3mapVertex_t){.accurate_height=8192,.waterlevel=8550,.level=2};
    S_ClearFlightSupport();S_InitFlightSupport();
    T_EQ(S_GetPreparedWaterSupport((vec2_t){20,20}),0);
    /* Non-stock Water.slk metadata must reach this producer without the
     * surface's tile scale. A missing tileset selects the native LSha row. */
    extern slkTestData_t *parse_slk_string(cstring_t);
    extern void free_slk_rows(slkTestData_t *);
    slkTestData_t *rows=parse_slk_string(
        "ID;PWXL;N;E\nB;X2;Y2;D0\nC;Y1;X1;K\"waterID\"\nC;X2;K\"height\"\n"
        "C;Y2;X1;K\"LSha\"\nC;X2;K12.5\nE\n");
    slkTestData_t *old=G_SetSLKRows("WaterData",rows);
    v[0].water=1;world.map->tileset='?';
    S_ClearFlightSupport();S_InitFlightSupport();
    T_EQ(S_GetPreparedWaterSupport((vec2_t){20,20}),102);
    G_SetSLKRows("WaterData",old);free_slk_rows(rows);
    S_ClearFlightSupport();reset_entities();setup_test_world();
}
#endif

#ifdef BZ_TESTS
#include "tests/fixtures/retail_preparedwater_ramp.h"
TEST(wc3_support, prepared_water_ramp_corners_match_repeated_native_producer) {
    extern void reset_entities(void),setup_test_world(void);
    reset_entities();setup_test_world();
    stbIniCache_t saved_misc=game.config.misc;game.config.misc=(stbIniCache_t){0};
    T_ASSERT(Stb_IniCacheLoadBuffer(&game.config.misc,"[Terrain]\nMaxHeight=1536\n"));
    world.map->width=world.map->height=17;world.map->center=(vec2_t){0,0};world.map->tileset='L';
    FOR_LOOP(y,17)FOR_LOOP(x,17) {
        bool ramp=(x==4 && y>=6 && y<=10) || (x>=7 && x<=10 && y>=6 && y<=10) || (x>=13 && y>=8);
        ((war3mapVertex_t *)world.map->vertices)[y*17+x]=(war3mapVertex_t){
            .accurate_height=8192,.waterlevel=6144,.water=1,.ramp=ramp,.level=2+(y>=8)};
    }
    S_InitFlightSupport();
    FOR_LOOP(y,16)FOR_LOOP(x,16)
        T_EQ(wc3_float_bits(S_GetPreparedWaterSupport((vec2_t){x*4+1,y*4+3})),preparedwater_ramp_grid[y*16+x]);
    Stb_IniCacheFree(&game.config.misc);game.config.misc=saved_misc;
    S_ClearFlightSupport();reset_entities();setup_test_world();
}
#endif

#ifdef BZ_TESTS
#include "tests/fixtures/retail_preparedwater_ramp_kernels.h"
TEST(wc3_support, prepared_water_all_ramp_topologies_match_original_instructions) {
    extern void reset_entities(void),setup_test_world(void);
    reset_entities();setup_test_world();world.map->width=world.map->height=7;
    T_EQ(sizeof(preparedwater_ramp_kernels)/sizeof(*preparedwater_ramp_kernels),28416);
    FOR_LOOP(i,sizeof(preparedwater_ramp_kernels)/sizeof(*preparedwater_ramp_kernels)) {
        typeof(preparedwater_ramp_kernels[0]) *c=preparedwater_ramp_kernels+i;
        FOR_LOOP(j,49)((war3mapVertex_t *)world.map->vertices)[j]=(war3mapVertex_t){
            .level=1+((c->layers[j/32]>>(j%32*2))&3),.ramp=(c->ramps>>j)&1};
        T_EQ((uint32_t)support_ramp_kind(c->x,c->y,c->cap),c->kind);
        T_EQ((uint32_t)support_ramp_type(c->x,c->y,c->cap),c->type);
        if(c->type=='S')T_EQ((uint32_t)support_ramp_corner(c->x,c->y,c->cap),c->corner);
        T_EQ(wc3_float_bits(support_ramp_offset(c->x,c->y,c->cap)),c->offset);
    }
    reset_entities();setup_test_world();
}
#endif

#ifdef BZ_TESTS
TEST(wc3_support, prepared_water_authored_height_cap_reaches_native_corner_fields) {
    extern void reset_entities(void),setup_test_world(void);
    reset_entities();setup_test_world();world.map->width=world.map->height=7;
    stbIniCache_t saved_misc=game.config.misc;game.config.misc=(stbIniCache_t){0};
    FOR_LOOP(i,sizeof(preparedwater_ramp_fields)/sizeof(*preparedwater_ramp_fields)) {
        typeof(preparedwater_ramp_fields[0]) *f=preparedwater_ramp_fields+i;
        typeof(preparedwater_ramp_kernels[0]) *c=preparedwater_ramp_kernels+f->index;
        char misc[80];snprintf(misc,sizeof(misc),"[Terrain]\nMaxHeight=%d\n",f->maximum);
        Stb_IniCacheFree(&game.config.misc);T_ASSERT(Stb_IniCacheLoadBuffer(&game.config.misc,misc));
        FOR_LOOP(j,49)((war3mapVertex_t *)world.map->vertices)[j]=(war3mapVertex_t){.accurate_height=8192,
            .level=1+((c->layers[j/32]>>(j%32*2))&3),.ramp=(c->ramps>>j)&1};
        S_InitFlightSupport();
        FOR_LOOP(y,6)FOR_LOOP(x,6)
            T_EQ(wc3_float_bits(S_GetPreparedWaterSupport((vec2_t){x*4+1,y*4+3})),f->values[y*6+x]);
    }
    Stb_IniCacheFree(&game.config.misc);game.config.misc=saved_misc;
    reset_entities();setup_test_world();
}
#endif

#ifdef BZ_TESTS
#include "tests/fixtures/retail_support_metadata.h"
TEST(wc3_support, prepared_height_metadata_matches_original_storm_and_sse) {
    FOR_LOOP(i,sizeof(support_metadata_cases)/sizeof(*support_metadata_cases)) {
        float value=support_metadata_float(support_metadata_cases[i].text);
        T_EQ(wc3_float_bits(value),support_metadata_cases[i].value);
        T_EQ((uint32_t)support_height_cap(value),support_metadata_cases[i].cap);
    }
}
#endif

#ifdef BZ_TESTS
TEST(wc3_support, prepared_height_metadata_ini_reaches_native_field) {
    extern void reset_entities(void),setup_test_world(void);
    reset_entities();setup_test_world();world.map->width=world.map->height=7;
    stbIniCache_t saved_misc=game.config.misc;game.config.misc=(stbIniCache_t){0};
    FOR_LOOP(i,sizeof(support_metadata_fields)/sizeof(*support_metadata_fields)) {
        typeof(support_metadata_fields[0]) *f=support_metadata_fields+i;
        typeof(preparedwater_ramp_kernels[0]) *c=preparedwater_ramp_kernels+f->index;
        char misc[128];snprintf(misc,sizeof(misc),"[Terrain]\nMaxHeight=%s\n",f->text);
        Stb_IniCacheFree(&game.config.misc);T_ASSERT(Stb_IniCacheLoadBuffer(&game.config.misc,misc));
        FOR_LOOP(j,49)((war3mapVertex_t *)world.map->vertices)[j]=(war3mapVertex_t){.accurate_height=8192,
            .level=1+((c->layers[j/32]>>(j%32*2))&3),.ramp=(c->ramps>>j)&1};
        S_InitFlightSupport();
        FOR_LOOP(y,6)FOR_LOOP(x,6)
            T_EQ(wc3_float_bits(S_GetPreparedWaterSupport((vec2_t){x*4+1,y*4+3})),f->values[y*6+x]);
    }
    Stb_IniCacheFree(&game.config.misc);game.config.misc=saved_misc;
    reset_entities();setup_test_world();
}
#endif
